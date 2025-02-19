/*
 * Copyright (C) 2007 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <ctype.h>

#include <fs_mgr.h>
#include "mtdutils/mtdutils.h"
#include "mounts.h"
#include "roots.h"
#include "common.h"
#include "make_ext4fs.h"
#include "libubi.h"
#include "cutils/properties.h"

#include <libgen.h>

#include "extendedcommands.h"
#include "flashutils/flashutils.h"
#include "recovery_ui.h"
#include "voldclient/voldclient.h"

#define DEFAULT_CTRL_DEV "/dev/ubi_ctrl"

static struct fstab *fstab = NULL;

extern struct selabel_handle *sehandle;
static int format_ubifs_volume(const char* location);

int get_num_volumes() {
    return fstab->num_entries;
}

Volume* get_device_volumes() {
    return fstab->recs;
}

void load_volume_table() {
    int i;
    int ret;

    fstab = fs_mgr_read_fstab("/etc/recovery.fstab");
    if (!fstab) {
        LOGE("failed to read /etc/recovery.fstab\n");
        return;
    }

    ret = fs_mgr_add_entry(fstab, "/tmp", "ramdisk", "ramdisk", 0);
    if (ret < 0 ) {
        LOGE("failed to add /tmp entry to fstab\n");
        fs_mgr_free_fstab(fstab);
        fstab = NULL;
        return;
    }

    // Process vold-managed volumes with mount point "auto"
    for (i = 0; i < fstab->num_entries; ++i) {
        Volume* v = &fstab->recs[i];
        if (fs_mgr_is_voldmanaged(v) && strcmp(v->mount_point, "auto") == 0) {
            char mount[PATH_MAX];

            // Set the mount point to /storage/label which as used by vold
            snprintf(mount, PATH_MAX, "/storage/%s", v->label);
            free(v->mount_point);
            v->mount_point = strdup(mount);
        }
    }

#ifdef BOARD_NATIVE_DUALBOOT_SINGLEDATA
    device_truedualboot_after_load_volume_table();
#endif

    fprintf(stderr, "recovery filesystem table\n");
    fprintf(stderr, "=========================\n");
    for (i = 0; i < fstab->num_entries; ++i) {
        Volume* v = &fstab->recs[i];
        fprintf(stderr, "  %d %s %s %s %lld\n", i, v->mount_point, v->fs_type,
               v->blk_device, v->length);
    }
    fprintf(stderr, "\n");
}

Volume* volume_for_path(const char* path) {
    return fs_mgr_get_entry_for_mount_point(fstab, path);
}

int is_primary_storage_voldmanaged() {
    Volume* v;
    v = volume_for_path("/storage/sdcard0");
    return fs_mgr_is_voldmanaged(v);
}

static char* primary_storage_path = NULL;
char* get_primary_storage_path() {
    if (primary_storage_path == NULL) {
        if (volume_for_path("/storage/sdcard0"))
            primary_storage_path = "/storage/sdcard0";
        else
            primary_storage_path = "/sdcard";
    }
    return primary_storage_path;
}

int get_num_extra_volumes() {
    int num = 0;
    int i;
    for (i = 0; i < get_num_volumes(); i++) {
        Volume* v = get_device_volumes() + i;
        if ((strcmp("/external_sd", v->mount_point) == 0) ||
                ((strcmp(get_primary_storage_path(), v->mount_point) != 0) &&
                fs_mgr_is_voldmanaged(v) && vold_is_volume_available(v->mount_point)))
            num++;
    }
    return num;
}

char** get_extra_storage_paths() {
    int i = 0, j = 0;
    static char* paths[MAX_NUM_MANAGED_VOLUMES];
    int num_extra_volumes = get_num_extra_volumes();

    if (num_extra_volumes == 0)
        return NULL;

    for (i = 0; i < get_num_volumes(); i++) {
        Volume* v = get_device_volumes() + i;
        if ((strcmp("/external_sd", v->mount_point) == 0) ||
                ((strcmp(get_primary_storage_path(), v->mount_point) != 0) &&
                fs_mgr_is_voldmanaged(v) && vold_is_volume_available(v->mount_point))) {
            paths[j] = v->mount_point;
            j++;
        }
    }
    paths[j] = NULL;

    return paths;
}

static char* android_secure_path = NULL;
char* get_android_secure_path() {
    if (android_secure_path == NULL) {
        android_secure_path = malloc(sizeof("/.android_secure") + strlen(get_primary_storage_path()) + 1);
        sprintf(android_secure_path, "%s/.android_secure", primary_storage_path);
    }
    return android_secure_path;
}

int try_mount(const char* device, const char* mount_point, const char* fs_type, const char* fs_options) {
    if (device == NULL || mount_point == NULL || fs_type == NULL)
        return -1;
    int ret = 0;
    if (fs_options == NULL) {
        ret = mount(device, mount_point, fs_type,
                       MS_NOATIME | MS_NODEV | MS_NODIRATIME, "");
    }
    else {
        char mount_cmd[PATH_MAX];
        sprintf(mount_cmd, "mount -t %s -o%s %s %s", fs_type, fs_options, device, mount_point);
        ret = __system(mount_cmd);
    }
    if (ret == 0)
        return 0;
    LOGW("failed to mount %s (%s)\n", device, strerror(errno));
    return ret;
}

int is_data_media() {
    int i;
    int has_sdcard = 0;
    for (i = 0; i < get_num_volumes(); i++) {
        Volume* vol = get_device_volumes() + i;
        if (strcmp(vol->fs_type, "datamedia") == 0)
            return 1;
        if (strcmp(vol->mount_point, "/sdcard") == 0)
            has_sdcard = 1;
        if (fs_mgr_is_voldmanaged(vol) &&
                (strcmp(vol->mount_point, "/storage/sdcard0") == 0))
            has_sdcard = 1;
    }
    return !has_sdcard;
}

void setup_data_media() {
    int i;
    char* mount_point = "/sdcard";
    for (i = 0; i < get_num_volumes(); i++) {
        Volume* vol = get_device_volumes() + i;
        if (strcmp(vol->fs_type, "datamedia") == 0) {
            mount_point = vol->mount_point;
            break;
        }
    }

    // recreate /data/media with proper permissions
    rmdir(mount_point);
    mkdir("/data/media", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
    symlink("/data/media", mount_point);
}

int is_data_media_volume_path(const char* path) {
    Volume* v = volume_for_path(path);
    if (v != NULL)
        return strcmp(v->fs_type, "datamedia") == 0;

    if (!is_data_media()) {
        return 0;
    }

    return strcmp(path, "/sdcard") == 0 || path == strstr(path, "/sdcard/");
}

int ensure_path_mounted(const char* path) {
    return ensure_path_mounted_at_mount_point(path, NULL);
}

int ensure_path_mounted_at_mount_point(const char* path, const char* mount_point) {
#ifdef BOARD_NATIVE_DUALBOOT_SINGLEDATA
	if(device_truedualboot_mount(path, mount_point) <= 0)
		return 0;
#endif

    if (is_data_media_volume_path(path)) {
        if (ui_should_log_stdout()) {
            LOGI("using /data/media for %s.\n", path);
        }
        int ret;
        if (0 != (ret = ensure_path_mounted("/data")))
            return ret;
        setup_data_media();
        return 0;
    }
    Volume* v = volume_for_path(path);
    if (v == NULL) {
        LOGE("unknown volume for path [%s]\n", path);
        return -1;
    }
    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // the ramdisk is always mounted.
        return 0;
    }

    int result;
    result = scan_mounted_volumes();
    if (result < 0) {
        LOGE("failed to scan mounted volumes\n");
        return -1;
    }

    if (NULL == mount_point)
        mount_point = v->mount_point;

    const MountedVolume* mv =
        find_mounted_volume_by_mount_point(mount_point);
    if (mv) {
        // volume is already mounted
        return 0;
    }

    mkdir(mount_point, 0755);  // in case it doesn't already exist

    if (fs_mgr_is_voldmanaged(v)) {
        return vold_mount_volume(mount_point, 1) == CommandOkay ? 0 : -1;

    } else if (strcmp(v->fs_type, "yaffs2") == 0) {
        // mount an MTD partition as a YAFFS2 filesystem.
        mtd_scan_partitions();
        const MtdPartition* partition;
        partition = mtd_find_partition_by_name(v->blk_device);
        if (partition == NULL) {
            LOGE("failed to find \"%s\" partition to mount at \"%s\"\n",
                 v->blk_device, mount_point);
            return -1;
        }
        return mtd_mount_partition(partition, mount_point, v->fs_type, 0);
    } else if (strcmp(v->fs_type, "ext4") == 0 ||
               strcmp(v->fs_type, "ext3") == 0 ||
               strcmp(v->fs_type, "rfs") == 0 ||
               strcmp(v->fs_type, "vfat") == 0) {
        if ((result = try_mount(v->blk_device, mount_point, v->fs_type, v->fs_options)) == 0)
            return 0;
        if ((result = try_mount(v->blk_device, mount_point, v->fs_type2, v->fs_options2)) == 0)
            return 0;
        if ((result = try_mount(v->blk_device2, mount_point, v->fs_type2, v->fs_options2)) == 0)
            return 0;
        return result;
    } else if (strcmp(v->fs_type, "ubifs") == 0) {
        LOGI("ensure_path_mounted ubifs:  %s %s %s %s\n", v->mount_point, v->fs_type,
               v->blk_device, v->blk_device2);
        libubi_t libubi;
        struct ubi_info ubi_info;
        struct ubi_dev_info dev_info;
        struct ubi_attach_request req;
        int err;
        char value[32] = {0};

        mtd_scan_partitions();
        int mtdn = mtd_get_index_by_name(v->blk_device);
        if (mtdn < 0) {
            LOGE("bad mtd index for %s\n", v->blk_device);
            return -1;
        }

        libubi = libubi_open();
        if (!libubi) {
            LOGE("libubi_open fail\n");
            return -1;
        }

        /*
         * Make sure the kernel is fresh enough and this feature is supported.
         */
        err = ubi_get_info(libubi, &ubi_info);
        if (err) {
            LOGE("cannot get UBI information\n");
            goto out_ubi_close;
        }

        if (ubi_info.ctrl_major == -1) {
            LOGE("MTD attach/detach feature is not supported by your kernel\n");
            goto out_ubi_close;
        }

        req.dev_num = UBI_DEV_NUM_AUTO;
        req.mtd_num = mtdn;
        req.vid_hdr_offset = 0;
        req.mtd_dev_node = NULL;

        // make sure partition is detached before attaching
        ubi_detach_mtd(libubi, DEFAULT_CTRL_DEV, mtdn);

        err = ubi_attach(libubi, DEFAULT_CTRL_DEV, &req);
        if (err) {
            LOGE("cannot attach mtd%d", mtdn);
            goto out_ubi_close;
        }

        /* Print some information about the new UBI device */
        err = ubi_get_dev_info1(libubi, req.dev_num, &dev_info);
        if (err) {
            LOGE("cannot get information about newly created UBI device\n");
            goto out_ubi_detach;
        }

        sprintf(value, "/dev/ubi0_userdata", dev_info.dev_num);

        /* Print information about the created device */
        //err = ubi_get_vol_info1(libubi, dev_info.dev_num, 0, &vol_info);
        //if (err) {
        //  LOGE("cannot get information about UBI volume 0");
        //  goto out_ubi_detach;
        //}

        if (mount(value, v->mount_point, v->fs_type,  MS_NOATIME | MS_NODEV | MS_NODIRATIME, NULL )) {
            LOGE("cannot mount ubifs %s to %s\n", value, v->mount_point);
            goto out_ubi_detach;
        }
        LOGI("mount ubifs successful  %s to %s\n", value, v->mount_point);

        libubi_close(libubi);
        return 0;

out_ubi_detach:
        ubi_detach_mtd(libubi, DEFAULT_CTRL_DEV, mtdn);

out_ubi_close:
        libubi_close(libubi);
        return -1;
    } else {
        // let's try mounting with the mount binary and hope for the best.
        char mount_cmd[PATH_MAX];
        sprintf(mount_cmd, "mount %s", mount_point);
        return __system(mount_cmd);
    }

    LOGE("unknown fs_type \"%s\" for %s\n", v->fs_type, mount_point);
    return -1;
}

int ensure_path_unmounted(const char* path) {
#ifdef BOARD_NATIVE_DUALBOOT_SINGLEDATA
	if(device_truedualboot_unmount(path) <= 0)
		return 0;
#endif
	int ret;

    // if we are using /data/media, do not ever unmount volumes /data or /sdcard
    if (is_data_media_volume_path(path)) {
        return ensure_path_unmounted("/data");
    }
    if (strstr(path, "/data") == path && is_data_media() && is_data_media_preserved()) {
        return 0;
    }

    Volume* v = volume_for_path(path);
    if (v == NULL) {
        LOGE("unknown volume for path [%s]\n", path);
        return -1;
    }

    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // the ramdisk is always mounted; you can't unmount it.
        return -1;
    }

    int result;
    result = scan_mounted_volumes();
    if (result < 0) {
        LOGE("failed to scan mounted volumes\n");
        return -1;
    }

    const MountedVolume* mv =
        find_mounted_volume_by_mount_point(v->mount_point);
    if (mv == NULL) {
        // volume is already unmounted
        return 0;
    }
    
    if (strcmp(v->fs_type, "ubifs") != 0) {
        return unmount_mounted_volume(mv);
    } else {
        libubi_t libubi;
        struct ubi_info ubi_info;

        unmount_mounted_volume(mv);

        mtd_scan_partitions();
        int mtdn = mtd_get_index_by_name(v->blk_device);
        if (mtdn < 0) {
            LOGE("bad mtd index for %s\n", v->blk_device);
            return -1;
        }

        libubi = libubi_open();
        if (!libubi) {
            LOGE("libubi_open fail\n");
            return -1;
        }

        /*
         * Make sure the kernel is fresh enough and this feature is supported.
         */
        ret = ubi_get_info(libubi, &ubi_info);
        if (ret) {
            LOGE("cannot get UBI information\n");
            goto out_ubi_close;
        }

        if (ubi_info.ctrl_major == -1) {
            LOGE("MTD detach/detach feature is not supported by your kernel\n");
            goto out_ubi_close;
        }

        ret = ubi_detach_mtd(libubi, DEFAULT_CTRL_DEV, mtdn);
        if (ret) {
            LOGE("cannot detach mtd%d\n", mtdn);
            goto out_ubi_close;
        }
        LOGI("detach ubifs successful mtd%d\n", mtdn);

        libubi_close(libubi);
        return 0;

    out_ubi_close:
        libubi_close(libubi);
        return -1;
    }

    if (fs_mgr_is_voldmanaged(volume_for_path(v->mount_point)))
        return vold_unmount_volume(v->mount_point, 0, 1) == CommandOkay ? 0 : -1;

    return unmount_mounted_volume(mv);
}

int format_volume(const char* volume) {
#ifdef BOARD_NATIVE_DUALBOOT_SINGLEDATA
    if(device_truedualboot_format_volume(volume) <= 0)
        return 0;
#endif

    if (is_data_media_volume_path(volume)) {
        return format_unknown_device(NULL, volume, NULL);
    }
    // check to see if /data is being formatted, and if it is /data/media
    // Note: the /sdcard check is redundant probably, just being safe.
    if (strstr(volume, "/data") == volume && is_data_media() && is_data_media_preserved()) {
        return format_unknown_device(NULL, volume, NULL);
    }

    Volume* v = volume_for_path(volume);
    if (v == NULL) {
        // silent failure for sd-ext
        if (strcmp(volume, "/sd-ext") != 0)
            LOGE("unknown volume '%s'\n", volume);
        return -1;
    }
    // silent failure to format non existing sd-ext when defined in recovery.fstab
    if (strcmp(volume, "/sd-ext") == 0) {
        struct stat s;
        if (0 != stat(v->blk_device, &s)) {
            LOGI("Skipping format of sd-ext\n");
            return -1;
        }
    }

    // Only use vold format for exact matches otherwise /sdcard will be
    // formatted instead of /storage/sdcard0/.android_secure
    if (fs_mgr_is_voldmanaged(v) && strcmp(volume, v->mount_point) == 0) {
        if (ensure_path_unmounted(volume) != 0) {
            LOGE("format_volume failed to unmount %s", v->mount_point);
        }
        if (strcmp(v->fs_type, "auto") == 0) {
            // Format with current filesystem
            return vold_format_volume(v->mount_point, 1) == CommandOkay ? 0 : -1;
        } else {
            // Format filesystem defined in fstab
            return vold_custom_format_volume(v->mount_point, v->fs_type, 1) == CommandOkay ? 0 : -1;
        }
    }

    if (strcmp(v->fs_type, "ramdisk") == 0) {
        // you can't format the ramdisk.
        LOGE("can't format_volume \"%s\"", volume);
        return -1;
    }
    if (strcmp(v->mount_point, volume) != 0) {
#if 0
        LOGE("can't give path \"%s\" to format_volume\n", volume);
        return -1;
#endif
        return format_unknown_device(v->blk_device, volume, NULL);
    }

    if (ensure_path_unmounted(volume) != 0) {
        LOGE("format_volume failed to unmount \"%s\"\n", v->mount_point);
        return -1;
    }

    if (strcmp(v->fs_type, "yaffs2") == 0 || strcmp(v->fs_type, "mtd") == 0 ||
        strcmp(v->fs_type, "ubifs") == 0) {
        mtd_scan_partitions();
        const MtdPartition* partition = mtd_find_partition_by_name(v->blk_device);
        if (partition == NULL) {
            LOGE("format_volume: no MTD partition \"%s\"\n", v->blk_device);
            return -1;
        }

        MtdWriteContext *write = mtd_write_partition(partition);
        if (write == NULL) {
            LOGW("format_volume: can't open MTD \"%s\"\n", v->blk_device);
            return -1;
        } else if (mtd_erase_blocks(write, -1) == (off_t) -1) {
            LOGW("format_volume: can't erase MTD \"%s\"\n", v->blk_device);
            mtd_write_close(write);
            return -1;
        } else if (mtd_write_close(write)) {
            LOGW("format_volume: can't close MTD \"%s\"\n", v->blk_device);
            return -1;
        }
        if (strcmp(v->fs_type, "ubifs") == 0) {
            return format_ubifs_volume(v->blk_device);
        }
        return 0;
    }

    if (strcmp(v->fs_type, "ext4") == 0) {
        int result = make_ext4fs(v->blk_device, v->length, volume, sehandle);
        if (result != 0) {
            LOGE("format_volume: make_extf4fs failed on %s\n", v->blk_device);
            return -1;
        }
        return 0;
    }

#ifdef USE_F2FS
    if (strcmp(v->fs_type, "f2fs") == 0) {
        char* args[] = { "mkfs.f2fs", v->blk_device };
        if (make_f2fs_main(2, args) != 0) {
            LOGE("format_volume: mkfs.f2fs failed on %s\n", v->blk_device);
            return -1;
        }
        return 0;
    }
#endif

#if 0
    LOGE("format_volume: fs_type \"%s\" unsupported\n", v->fs_type);
    return -1;
#endif
    return format_unknown_device(v->blk_device, volume, v->fs_type);
}

static int format_ubifs_volume(const char* location) {
    int err;
    struct ubi_info ubi_info;
    struct ubi_dev_info dev_info;
    struct ubi_attach_request req;
    struct ubi_mkvol_request req2;
    char ubinode[16] ={0};

    mtd_scan_partitions();
    int mtdn = mtd_get_index_by_name(location);
    if (mtdn < 0) {
        LOGE("bad mtd index for %s\n", location);
        return -1;
    }

    libubi_t libubi;
    libubi = libubi_open();
    if (!libubi) {
        LOGE("libubi_open fail\n");
        return -1;
    }

    /*
     * Make sure the kernel is fresh enough and this feature is supported.
     */
    err = ubi_get_info(libubi, &ubi_info);
    if (err) {
        LOGE("cannot get UBI information\n");
        goto out_ubi_close;
    }

    if (ubi_info.ctrl_major == -1) {
        LOGE("MTD attach/detach feature is not supported by your kernel\n");
        goto out_ubi_close;
    }

    req.dev_num = UBI_DEV_NUM_AUTO;
    req.mtd_num = mtdn;
    req.vid_hdr_offset = 0;
    req.mtd_dev_node = NULL;

    err = ubi_attach(libubi, DEFAULT_CTRL_DEV, &req);
    if (err) {
        LOGE("cannot attach mtd%d", mtdn);
        goto out_ubi_close;
    }

    /* Print some information about the new UBI device */
    err = ubi_get_dev_info1(libubi, req.dev_num, &dev_info);
    if (err) {
        LOGE("cannot get information about newly created UBI device\n");
        goto out_ubi_detach;
    }

    req2.vol_id = UBI_VOL_NUM_AUTO;
    req2.alignment = 1;
    req2.bytes = dev_info.avail_lebs*dev_info.leb_size;
    req2.name = location;
    req2.vol_type = UBI_DYNAMIC_VOLUME;

    sprintf(ubinode, "/dev/ubi0_userdata", dev_info.dev_num);

    err = ubi_mkvol(libubi, ubinode, &req2);
    if (err < 0) {
        LOGE("cannot UBI create volume %s at %s %d %llu\n", req2.name, ubinode ,err, req2.bytes);
        goto out_ubi_detach;
    }

    ubi_detach_mtd(libubi, DEFAULT_CTRL_DEV, mtdn);
    libubi_close(libubi);
    return 0;

out_ubi_detach:
    ubi_detach_mtd(libubi, DEFAULT_CTRL_DEV, mtdn);

out_ubi_close:
    libubi_close(libubi);
    return -1;
}

static int data_media_preserved_state = 1;
void preserve_data_media(int val) {
    data_media_preserved_state = val;
}

int is_data_media_preserved() {
    return data_media_preserved_state;
}

void setup_legacy_storage_paths() {
    char* primary_path = get_primary_storage_path();

    if (!is_data_media_volume_path(primary_path)) {
        rmdir("/sdcard");
        symlink(primary_path, "/sdcard");
    }
}
