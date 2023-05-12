#TARGET_NO_RECOVERY := true
#BOARD_USES_FULL_RECOVERY_IMAGE := true

# Recovery
RECOVERY_FSTAB_VERSION := 2
TARGET_RECOVERY_FSTAB := $(COMMON_PATH)/rootdir/fstab.qcom
#TARGET_RECOVERY_FSTAB := $(COMMON_PATH)/recovery/recovery.fstab

RECOVERY_VARIANT := twrp
TW_THEME := portrait_mdpi

# TWRP specific build flags
DEVICE_RESOLUTION := 540x960
RECOVERY_SDCARD_ON_DATA := true
BOARD_HAS_NO_REAL_SDCARD := true
TW_INTERNAL_STORAGE_PATH := "/data/media"
TW_INTERNAL_STORAGE_MOUNT_POINT := "data"
TW_EXTERNAL_STORAGE_PATH := "/external_sd"
TW_EXTERNAL_STORAGE_MOUNT_POINT := "external_sd"
TW_NO_USB_STORAGE := true
RECOVERY_GRAPHICS_USE_LINELENGTH := true
TARGET_RECOVERY_PIXEL_FORMAT := "RGBX_8888"
TW_MAX_BRIGHTNESS := 255
