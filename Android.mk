ifeq ($(call my-dir),$(call project-path-for,recovery))

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

commands_recovery_local_path := $(LOCAL_PATH)
# LOCAL_CPP_EXTENSION := .c

LOCAL_SRC_FILES := \
    recovery.c \
    bootloader.c \
    install.c \
    roots.c \
    ui.c \
    mounts.c \
    extendedcommands.c \
    nandroid.c \
    nandroid_md5.c \
    reboot.c \
    ../../system/core/toolbox/dynarray.c \
    ../../system/core/toolbox/newfs_msdos.c \
    firmware.c \
    edifyscripting.c \
    prop.c \
    adb_install.c \
    verifier.c \
    ../../system/vold/vdc.c \
    propsrvc/legacy_property_service.c

LOCAL_SRC_FILES += \
    ubiutils-common.c \
    libubi.c
 
ADDITIONAL_RECOVERY_FILES := $(shell echo $$ADDITIONAL_RECOVERY_FILES)
LOCAL_SRC_FILES += $(ADDITIONAL_RECOVERY_FILES)

LOCAL_MODULE := recovery

LOCAL_FORCE_STATIC_EXECUTABLE := true

RECOVERY_FSTAB_VERSION := 2

ifdef I_AM_KOUSH
RECOVERY_NAME := ClockworkMod Recovery
LOCAL_CFLAGS += -DI_AM_KOUSH
else
ifndef RECOVERY_NAME
RECOVERY_NAME := Carliv Touch Recovery
endif
endif

RECOVERY_VERSION := $(RECOVERY_NAME) v3.0_kitkat

LOCAL_CFLAGS += -DRECOVERY_VERSION="$(RECOVERY_VERSION)"
RECOVERY_API_VERSION := 2
LOCAL_CFLAGS += -DRECOVERY_API_VERSION=$(RECOVERY_API_VERSION)

ifdef BOARD_TOUCH_RECOVERY
ifeq ($(BOARD_USE_CUSTOM_RECOVERY_FONT),)
  BOARD_USE_CUSTOM_RECOVERY_FONT := \"roboto_15x24.h\"
endif
endif

ifeq ($(BOARD_USE_CUSTOM_RECOVERY_FONT),)
  BOARD_USE_CUSTOM_RECOVERY_FONT := \"font_10x18.h\"
endif

BOARD_RECOVERY_CHAR_WIDTH := $(shell echo $(BOARD_USE_CUSTOM_RECOVERY_FONT) | cut -d _  -f 2 | cut -d . -f 1 | cut -d x -f 1)
BOARD_RECOVERY_CHAR_HEIGHT := $(shell echo $(BOARD_USE_CUSTOM_RECOVERY_FONT) | cut -d _  -f 2 | cut -d . -f 1 | cut -d x -f 2)

RECOVERY_BUILD_DATE := $(shell date +"%Y-%m-%d")
RECOVERY_BUILD_USER := $(shell whoami)
RECOVERY_BUILD_HOST := $(shell hostname)
LOCAL_CFLAGS += -DBOARD_RECOVERY_CHAR_WIDTH=$(BOARD_RECOVERY_CHAR_WIDTH) -DBOARD_RECOVERY_CHAR_HEIGHT=$(BOARD_RECOVERY_CHAR_HEIGHT) -DRECOVERY_BUILD_DATE="$(RECOVERY_BUILD_DATE)" -DRECOVERY_BUILD_USER="$(RECOVERY_BUILD_USER)" -DRECOVERY_BUILD_HOST="$(RECOVERY_BUILD_HOST)"

BOARD_RECOVERY_DEFINES := BOARD_HAS_NO_SELECT_BUTTON BOARD_UMS_LUNFILE BOARD_RECOVERY_ALWAYS_WIPES BOARD_RECOVERY_HANDLES_MOUNT BOARD_TOUCH_RECOVERY RECOVERY_EXTEND_NANDROID_MENU TARGET_USE_CUSTOM_LUN_FILE_PATH TARGET_DEVICE TARGET_RECOVERY_FSTAB BOARD_NATIVE_DUALBOOT BOARD_NATIVE_DUALBOOT_SINGLEDATA BOARD_HAS_MTK

$(foreach board_define,$(BOARD_RECOVERY_DEFINES), \
  $(if $($(board_define)), \
    $(eval LOCAL_CFLAGS += -D$(board_define)=\"$($(board_define))\") \
  ) \
  )

ifneq ($(BOARD_RECOVERY_BLDRMSG_OFFSET),)
    LOCAL_CFLAGS += -DBOARD_RECOVERY_BLDRMSG_OFFSET=$(BOARD_RECOVERY_BLDRMSG_OFFSET)
endif

LOCAL_STATIC_LIBRARIES :=

LOCAL_CFLAGS += -DUSE_EXT4 -DMINIVOLD
LOCAL_C_INCLUDES += system/extras/ext4_utils system/core/fs_mgr/include external/fsck_msdos
LOCAL_C_INCLUDES += system/vold external/openssl/include

LOCAL_STATIC_LIBRARIES += libext4_utils_static libz libsparse_static

ifeq ($(ENABLE_LOKI_RECOVERY),true)
  LOCAL_CFLAGS += -DENABLE_LOKI
  LOCAL_STATIC_LIBRARIES += libloki_static
  LOCAL_SRC_FILES += loki/loki_recovery.c
endif

# This binary is in the recovery ramdisk, which is otherwise a copy of root.
# It gets copied there in config/Makefile.  LOCAL_MODULE_TAGS suppresses
# a (redundant) copy of the binary in /system/bin for user builds.
# TODO: Build the ramdisk image in a more principled way.

LOCAL_MODULE_TAGS := eng

ifeq ($(BOARD_CUSTOM_RECOVERY_KEYMAPPING),)
  LOCAL_SRC_FILES += default_recovery_keys.c
else
  LOCAL_SRC_FILES += $(BOARD_CUSTOM_RECOVERY_KEYMAPPING)
endif

ifeq ($(BOARD_CUSTOM_RECOVERY_UI),)
  LOCAL_SRC_FILES += default_recovery_ui.c
else
  LOCAL_SRC_FILES += $(BOARD_CUSTOM_RECOVERY_UI)
endif

LOCAL_STATIC_LIBRARIES += libvoldclient libsdcard libminipigz libfsck_msdos
LOCAL_STATIC_LIBRARIES += libmake_ext4fs libext4_utils_static libz libsparse_static

ifeq ($(TARGET_USERIMAGES_USE_F2FS), true)
LOCAL_CFLAGS += -DUSE_F2FS
LOCAL_STATIC_LIBRARIES += libmake_f2fs libfsck_f2fs libfibmap_f2fs
endif

LOCAL_STATIC_LIBRARIES += libminzip libunz libmincrypt

LOCAL_STATIC_LIBRARIES += libminizip libminadbd libedify libbusybox libmkyaffs2image libunyaffs liberase_image libdump_image libflash_image
LOCAL_LDFLAGS += -Wl,--no-fatal-warnings

LOCAL_STATIC_LIBRARIES += libfs_mgr libdedupe libcrypto_static libcrecovery libflashutils libmtdutils libmmcutils libbmlutils

ifeq ($(BOARD_USES_BML_OVER_MTD),true)
LOCAL_STATIC_LIBRARIES += libbml_over_mtd
endif

LOCAL_STATIC_LIBRARIES += libminui libpixelflinger_static libpng libcutils liblog
LOCAL_STATIC_LIBRARIES += libstdc++ libc

LOCAL_STATIC_LIBRARIES += libselinux
LOCAL_STATIC_LIBRARIES += libcrypto_static

RECOVERY_LINKS := bu make_ext4fs edify busybox flash_image dump_image mkyaffs2image unyaffs erase_image nandroid reboot volume setprop getprop start stop dedupe minizip setup_adbd fsck_msdos newfs_msdos vdc sdcard pigz

ifeq ($(TARGET_USERIMAGES_USE_F2FS), true)
RECOVERY_LINKS += mkfs.f2fs fsck.f2fs fibmap.f2fs
endif

# nc is provided by external/netcat
RECOVERY_SYMLINKS := $(addprefix $(TARGET_RECOVERY_ROOT_OUT)/sbin/,$(RECOVERY_LINKS))

BUSYBOX_LINKS := $(shell cat external/busybox/busybox-minimal.links)
exclude := tune2fs mke2fs
RECOVERY_BUSYBOX_SYMLINKS := $(addprefix $(TARGET_RECOVERY_ROOT_OUT)/sbin/,$(filter-out $(exclude),$(notdir $(BUSYBOX_LINKS))))

LOCAL_ADDITIONAL_DEPENDENCIES := \
    killrecovery.sh \
    parted \
    sdparted \
    su.recovery \
    install-su.sh \
    run-su-daemon.sh

LOCAL_ADDITIONAL_DEPENDENCIES += \
    minivold \
    recovery_e2fsck \
    recovery_mke2fs \
    recovery_tune2fs \
    mount.exfat_static

LOCAL_ADDITIONAL_DEPENDENCIES += $(RECOVERY_SYMLINKS) $(RECOVERY_BUSYBOX_SYMLINKS)

LOCAL_ADDITIONAL_DEPENDENCIES += carliv

ifneq ($(TARGET_RECOVERY_DEVICE_MODULES),)
    LOCAL_ADDITIONAL_DEPENDENCIES += $(TARGET_RECOVERY_DEVICE_MODULES)
endif

include $(BUILD_EXECUTABLE)

$(RECOVERY_SYMLINKS): RECOVERY_BINARY := $(LOCAL_MODULE)
$(RECOVERY_SYMLINKS):
	@echo "Symlink: $@ -> $(RECOVERY_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf $(RECOVERY_BINARY) $@

# Now let's do recovery symlinks
$(RECOVERY_BUSYBOX_SYMLINKS): BUSYBOX_BINARY := busybox
$(RECOVERY_BUSYBOX_SYMLINKS):
	@echo "Symlink: $@ -> $(BUSYBOX_BINARY)"
	@mkdir -p $(dir $@)
	@rm -rf $@
	$(hide) ln -sf $(BUSYBOX_BINARY) $@

include $(CLEAR_VARS)
LOCAL_MODULE := killrecovery.sh
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := RECOVERY_EXECUTABLES
LOCAL_MODULE_PATH := $(TARGET_RECOVERY_ROOT_OUT)/sbin
LOCAL_SRC_FILES := killrecovery.sh
include $(BUILD_PREBUILT)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := verifier_test.c verifier.c

LOCAL_C_INCLUDES += system/extras/ext4_utils system/core/fs_mgr/include

LOCAL_MODULE := verifier_test

LOCAL_FORCE_STATIC_EXECUTABLE := true

LOCAL_MODULE_TAGS := tests

LOCAL_STATIC_LIBRARIES := libmincrypt libcutils libstdc++ libc

include $(BUILD_EXECUTABLE)

include $(commands_recovery_local_path)/bmlutils/Android.mk
include $(commands_recovery_local_path)/dedupe/Android.mk
include $(commands_recovery_local_path)/flashutils/Android.mk
include $(commands_recovery_local_path)/libcrecovery/Android.mk
include $(commands_recovery_local_path)/minui/Android.mk
include $(commands_recovery_local_path)/minelf/Android.mk
include $(commands_recovery_local_path)/devices/Android.mk
include $(commands_recovery_local_path)/minzip/Android.mk
include $(commands_recovery_local_path)/minadbd/Android.mk
include $(commands_recovery_local_path)/mtdutils/Android.mk
include $(commands_recovery_local_path)/mmcutils/Android.mk
include $(commands_recovery_local_path)/tools/Android.mk
include $(commands_recovery_local_path)/edify/Android.mk
include $(commands_recovery_local_path)/updater/Android.mk
include $(commands_recovery_local_path)/applypatch/Android.mk
include $(commands_recovery_local_path)/utilities/Android.mk
include $(commands_recovery_local_path)/su/Android.mk
include $(commands_recovery_local_path)/voldclient/Android.mk
include $(commands_recovery_local_path)/loki/Android.mk
commands_recovery_local_path :=

endif
