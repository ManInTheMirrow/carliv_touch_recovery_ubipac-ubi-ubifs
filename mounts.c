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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mount.h>

#ifdef BOARD_HAS_MTK
#include <ctype.h>
#include <unistd.h>
#include <sys/limits.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <libgen.h>
#include "common.h"
#include "roots.h"
#endif

#include "mounts.h"

typedef struct {
    MountedVolume *volumes;
    int volumes_allocd;
    int volume_count;
} MountsState;

static MountsState g_mounts_state = {
    NULL,   // volumes
    0,      // volumes_allocd
    0       // volume_count
};

static inline void
free_volume_internals(const MountedVolume *volume, int zero)
{
    free((char *)volume->device);
    free((char *)volume->mount_point);
    free((char *)volume->filesystem);
    free((char *)volume->flags);
    if (zero) {
        memset((void *)volume, 0, sizeof(*volume));
    }
}

#define PROC_MOUNTS_FILENAME   "/proc/mounts"

int
scan_mounted_volumes()
{
    char buf[2048];
    const char *bufp;
    int fd;
    ssize_t nbytes;

    if (g_mounts_state.volumes == NULL) {
        const int numv = 32;
        MountedVolume *volumes = malloc(numv * sizeof(*volumes));
        if (volumes == NULL) {
            errno = ENOMEM;
            return -1;
        }
        g_mounts_state.volumes = volumes;
        g_mounts_state.volumes_allocd = numv;
        memset(volumes, 0, numv * sizeof(*volumes));
    } else {
        /* Free the old volume strings.
         */
        int i;
        for (i = 0; i < g_mounts_state.volume_count; i++) {
            free_volume_internals(&g_mounts_state.volumes[i], 1);
        }
    }
    g_mounts_state.volume_count = 0;

    /* Open and read the file contents.
     */
    fd = open(PROC_MOUNTS_FILENAME, O_RDONLY);
    if (fd < 0) {
        goto bail;
    }
    nbytes = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (nbytes < 0) {
        goto bail;
    }
    buf[nbytes] = '\0';

    /* Parse the contents of the file, which looks like:
     *
     *     # cat /proc/mounts
     *     rootfs / rootfs rw 0 0
     *     /dev/pts /dev/pts devpts rw 0 0
     *     /proc /proc proc rw 0 0
     *     /sys /sys sysfs rw 0 0
     *     /dev/block/mtdblock4 /system yaffs2 rw,nodev,noatime,nodiratime 0 0
     *     /dev/block/mtdblock5 /data yaffs2 rw,nodev,noatime,nodiratime 0 0
     *     /dev/block/mmcblk0p1 /sdcard vfat rw,sync,dirsync,fmask=0000,dmask=0000,codepage=cp437,iocharset=iso8859-1,utf8 0 0
     *
     * The zeroes at the end are dummy placeholder fields to make the
     * output match Linux's /etc/mtab, but don't represent anything here.
     */
    bufp = buf;
    while (nbytes > 0) {
        char device[PATH_MAX];
        char mount_point[PATH_MAX];
        char filesystem[64];
        char flags[256];
        int matches;

        /* %as is a gnu extension that malloc()s a string for each field.
         */
        matches = sscanf(bufp, "%63s %63s %63s %127s",
                device, mount_point, filesystem, flags);

        if (matches == 4) {
            device[sizeof(device)-1] = '\0';
            mount_point[sizeof(mount_point)-1] = '\0';
            filesystem[sizeof(filesystem)-1] = '\0';
            flags[sizeof(flags)-1] = '\0';

            MountedVolume *v =
                    &g_mounts_state.volumes[g_mounts_state.volume_count++];
            v->device = strdup(device);
            v->mount_point = strdup(mount_point);
            v->filesystem = strdup(filesystem);
            v->flags = strdup(flags);
        } else {
printf("matches was %d on <<%.40s>>\n", matches, bufp);
        }

        /* Eat the line.
         */
        while (nbytes > 0 && *bufp != '\n') {
            bufp++;
            nbytes--;
        }
        if (nbytes > 0) {
            bufp++;
            nbytes--;
        }
    }

    return 0;

bail:
//TODO: free the strings we've allocated.
    g_mounts_state.volume_count = 0;
    return -1;
}

const MountedVolume *
find_mounted_volume_by_device(const char *device)
{
    if (g_mounts_state.volumes != NULL) {
        int i;
        for (i = 0; i < g_mounts_state.volume_count; i++) {
            MountedVolume *v = &g_mounts_state.volumes[i];
            /* May be null if it was unmounted and we haven't rescanned.
             */
            if (v->device != NULL) {
                if (strcmp(v->device, device) == 0) {
                    return v;
                }
            }
        }
    }
    return NULL;
}

const MountedVolume *
find_mounted_volume_by_mount_point(const char *mount_point)
{
    if (g_mounts_state.volumes != NULL) {
        int i;
        for (i = 0; i < g_mounts_state.volume_count; i++) {
            MountedVolume *v = &g_mounts_state.volumes[i];
            /* May be null if it was unmounted and we haven't rescanned.
             */
            if (v->mount_point != NULL) {
                if (strcmp(v->mount_point, mount_point) == 0) {
                    return v;
                }
            }
        }
    }
    return NULL;
}

const char *
get_filesystem_by_mounted_volume(const MountedVolume *vol)
{
    if (vol)
        return vol->filesystem;
    return NULL;
}

const char *
get_device_by_mounted_volume(const MountedVolume *vol)
{
    if (vol)
        return vol->device;
    return NULL;
}

int
unmount_mounted_volume(const MountedVolume *volume)
{
    /* Intentionally pass NULL to umount if the caller tries
     * to unmount a volume they already unmounted using this
     * function.
     */
    int ret = umount(volume->mount_point);
    if (ret == 0) {
        free_volume_internals(volume, 1);
        return 0;
    }
    return ret;
}

int
remount_read_only(const MountedVolume* volume)
{
    return mount(volume->device, volume->mount_point, volume->filesystem,
                 MS_NOATIME | MS_NODEV | MS_NODIRATIME |
                 MS_RDONLY | MS_REMOUNT, 0);
}

const MountedVolume *
find_mounted_volume_by_real_node(const char *node)
{
    if (g_mounts_state.volumes != NULL) {
        int i;
        for (i = 0; i < g_mounts_state.volume_count; i++) {
            MountedVolume *v = &g_mounts_state.volumes[i];
            /* May be null if it was unmounted and we haven't rescanned.
             */
            if (v->device != NULL) {
                ssize_t len;
                char path_resolved[PATH_MAX];
                if((len = readlink(v->device, path_resolved, sizeof(path_resolved)-1)) != -1)
                    path_resolved[len] = '\0';

                if (strcmp(path_resolved, node) == 0) {
                    return v;
                }
            }
        }
    }
    return NULL;
}

//=========================================/
//=   dynamic get size of MTK partitions  =/
//=    original work of Dees_Troy - TWRP  =/
//=     ported and adapted by carliv@xda  =/
//=    from PhilZ (PhilZ Touch Recovery)  =/
//=========================================/

#ifdef BOARD_HAS_MTK

unsigned long long mtk_size = 0;

int mtk_p_size(const char* path) {
    char buf[512];
    char tmpdevice[1024];
    FILE* fp_info;
    Volume* volume;
    
    if (is_data_media_volume_path(path))
        volume = volume_for_path("/data");
    else
        volume = volume_for_path(path);

    if (volume == NULL) {
        LOGE("Failed to find partition size '%s'\n", path);
        LOGE("  > invalid volume %s\n", path);
        return -1;
    }

    fp_info = fopen("/proc/dumchar_info", "rt");
    if (fp_info != NULL) {
        while (fgets(buf, sizeof(buf), fp_info) != NULL) {
            char p_name[32], p_actname[64];
            unsigned long p_size = 0;

            sscanf(buf, "%s %lx %*x %*u %s", p_name, &p_size, p_actname);
            
            if ((strncmp(p_actname, "/dev/", 5) != 0) || (strlen(buf) < 8))
                continue;
                
            sprintf(tmpdevice, "/dev/");
            strcat(tmpdevice, p_name);
            
            if (volume_for_path("/boot") != NULL)
				volume->blk_device = "/dev/bootimg";
            
            if (volume->blk_device != NULL && strcmp(tmpdevice, volume->blk_device) == 0) {
                mtk_size = p_size;
                fclose(fp_info);
                return 0;
            }
            
            if (volume->blk_device2 != NULL && strcmp(tmpdevice, volume->blk_device) == 0) {
                mtk_size = p_size;
                fclose(fp_info);
                return 0;
            }
        }

        fclose(fp_info);
    }
    int ret = -1;
    
    if (ret != 0)
        LOGE("Failed to find partition size '%s'\n", path);
    return ret;
}
#endif
