LOCAL_PATH := $(call my-dir)

DEVICE_BOOTDIR := device/sony/blue-common/boot
DEVICE_CMDLINE := $(DEVICE_BOOTDIR)/cmdline.txt
DEVICE_LOGORLE := $(DEVICE_BOOTDIR)/logo.rle
DEVICE_RPMBIN := $(DEVICE_BOOTDIR)/RPM.bin
INITSONY := $(PRODUCT_OUT)/utilities/init_sony
MKELF := $(DEVICE_BOOTDIR)/mkelf.py

MKELF_ARGS :=
BOARD_KERNEL_NAME := $(strip $(BOARD_KERNEL_NAME))
ifdef BOARD_KERNEL_NAME
  MKELF_ARGS += -n $(BOARD_KERNEL_NAME)
endif

uncompressed_ramdisk := $(PRODUCT_OUT)/ramdisk.cpio
$(uncompressed_ramdisk): $(INSTALLED_RAMDISK_TARGET)
	$(hide) $(MKBOOTFS) -d $(TARGET_OUT) $(TARGET_RAMDISK_OUT) > $@

recovery_uncompressed_ramdisk := $(PRODUCT_OUT)/ramdisk-recovery.cpio
recovery_uncompressed_device_ramdisk := $(PRODUCT_OUT)/ramdisk-recovery-device.cpio
$(recovery_uncompressed_device_ramdisk): $(MKBOOTFS) \
		$(INTERNAL_ROOT_FILES) \
		$(INSTALLED_RAMDISK_TARGET) \
		$(INTERNAL_RECOVERYIMAGE_FILES) \
		$(recovery_sepolicy) \
		$(INSTALLED_2NDBOOTLOADER_TARGET) \
		$(INSTALLED_RECOVERY_BUILD_PROP_TARGET) \
		$(recovery_resource_deps) $(recovery_root_deps) \
		$(recovery_fstab) \
		$(RECOVERY_INSTALL_OTA_KEYS) \
		$(INTERNAL_BOOTIMAGE_FILES)
	# Making recovery image
	mkdir -p $(TARGET_RECOVERY_OUT)
	mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/sdcard $(TARGET_RECOVERY_ROOT_OUT)/tmp
	# Copying baseline ramdisk...
	# Use rsync because "cp -Rf" fails to overwrite broken symlinks on Mac.
	rsync -a --exclude=sdcard $(IGNORE_RECOVERY_SEPOLICY) $(IGNORE_CACHE_LINK) $(TARGET_ROOT_OUT) $(TARGET_RECOVERY_OUT)
	# Modifying ramdisk contents...
	$(if $(filter true,$(BOARD_BUILD_SYSTEM_ROOT_IMAGE)),, \
	  ln -sf /system/bin/init $(TARGET_RECOVERY_ROOT_OUT)/init)
	# Removes $(TARGET_RECOVERY_ROOT_OUT)/init*.rc EXCEPT init.recovery*.rc.
	find $(TARGET_RECOVERY_ROOT_OUT) -maxdepth 1 -name 'init*.rc' -type f -not -name "init.recovery.*.rc" | xargs rm -f
	cp $(TARGET_ROOT_OUT)/init.recovery.*.rc $(TARGET_RECOVERY_ROOT_OUT)/ 2> /dev/null || true # Ignore error when the src file doesn't exist.
	mkdir -p $(TARGET_RECOVERY_ROOT_OUT)/res
	rm -rf $(TARGET_RECOVERY_ROOT_OUT)/res/*
	cp -rf $(recovery_resources_common)/* $(TARGET_RECOVERY_ROOT_OUT)/res
	$(foreach recovery_text_file,$(generated_recovery_text_files), \
	  cp -rf $(recovery_text_file) $(TARGET_RECOVERY_ROOT_OUT)/res/images/ &&) true
	cp -f $(recovery_font) $(TARGET_RECOVERY_ROOT_OUT)/res/images/font.png
	$(foreach item,$(recovery_root_private), \
	  cp -rf $(item) $(TARGET_RECOVERY_OUT)/;)
	$(foreach item,$(TARGET_PRIVATE_RES_DIRS), \
	  cp -rf $(item) $(TARGET_RECOVERY_ROOT_OUT)/$(newline);)
	$(foreach item,$(recovery_fstab), \
	  cp -f $(item) $(TARGET_RECOVERY_ROOT_OUT)/system/etc/recovery.fstab;)
	$(if $(strip $(recovery_wipe)), \
	  cp -f $(recovery_wipe) $(TARGET_RECOVERY_ROOT_OUT)/system/etc/recovery.wipe)
	ln -sf prop.default $(TARGET_RECOVERY_ROOT_OUT)/default.prop
	$(BOARD_RECOVERY_IMAGE_PREPARE)
	@echo ----- Making uncompressed recovery ramdisk ------
	cp $(DEVICE_LOGORLE) $(TARGET_RECOVERY_ROOT_OUT)/
	$(MKBOOTFS) $(TARGET_RECOVERY_ROOT_OUT) > $@
	rm -f $(recovery_uncompressed_ramdisk)
	cp $(recovery_uncompressed_device_ramdisk) $(recovery_uncompressed_ramdisk)

recovery_ramdisk := $(PRODUCT_OUT)/ramdisk-recovery.img
recovery_ramdisk_device := $(PRODUCT_OUT)/ramdisk-recovery-device.img
$(recovery_ramdisk_device): $(MINIGZIP) \
		$(recovery_uncompressed_device_ramdisk)
	$(hide) $(MINIGZIP) < $(recovery_uncompressed_ramdisk) > $@
	$(hide) cp -a $@ $(recovery_ramdisk)





INSTALLED_BOOTIMAGE_TARGET := $(PRODUCT_OUT)/boot.img
$(INSTALLED_BOOTIMAGE_TARGET): $(PRODUCT_OUT)/kernel \
		$(uncompressed_ramdisk) \
		$(recovery_uncompressed_device_ramdisk) \
		$(INTERNAL_ROOT_FILES) \
		$(INSTALLED_RAMDISK_TARGET) \
		$(INITSONY) \
		$(TARGET_RECOVERY_ROOT_OUT)/system/bin/toybox_static \
		$(PRODUCT_OUT)/utilities/keycheck \
		$(MKBOOTIMG) $(MINIGZIP) \
		$(INTERNAL_BOOTIMAGE_FILES)
	$(hide) rm -fr $(PRODUCT_OUT)/combinedroot
	$(hide) cp -a $(TARGET_RAMDISK_OUT) $(PRODUCT_OUT)/combinedroot
	$(hide) mkdir -p $(PRODUCT_OUT)/combinedroot/bin

	$(hide) cp $(DEVICE_LOGORLE) $(PRODUCT_OUT)/combinedroot/logo.rle
	$(hide) cp $(recovery_uncompressed_ramdisk) $(PRODUCT_OUT)/combinedroot/bin/
	$(hide) cp $(PRODUCT_OUT)/utilities/keycheck $(PRODUCT_OUT)/combinedroot/bin/
	$(hide) cp $(TARGET_RECOVERY_ROOT_OUT)/system/bin/toybox_static $(PRODUCT_OUT)/combinedroot/bin/toybox_init

	$(hide) cp $(INITSONY) $(PRODUCT_OUT)/combinedroot/bin/init_sony
	$(hide) chmod 755 $(PRODUCT_OUT)/combinedroot/bin/init_sony
	$(hide) mv $(PRODUCT_OUT)/combinedroot/init $(PRODUCT_OUT)/combinedroot/init.real
	$(hide) ln -s bin/init_sony $(PRODUCT_OUT)/combinedroot/init

	$(hide) $(MKBOOTFS) $(PRODUCT_OUT)/combinedroot/ > $(PRODUCT_OUT)/combinedroot.cpio
	$(hide) cat $(PRODUCT_OUT)/combinedroot.cpio | gzip > $(PRODUCT_OUT)/combinedroot.fs
	$(hide) python $(MKELF) $(MKELF_ARGS) -o $@ $(PRODUCT_OUT)/kernel@0x80208000 $(PRODUCT_OUT)/combinedroot.fs@0x81900000,ramdisk $(DEVICE_RPMBIN)@0x00020000,rpm $(DEVICE_CMDLINE)@cmdline

	$(hide) $(call assert-max-image-size,$@,$(BOARD_BOOTIMAGE_PARTITION_SIZE))

INSTALLED_RECOVERYIMAGE_TARGET := $(PRODUCT_OUT)/recovery.img
$(INSTALLED_RECOVERYIMAGE_TARGET): $(MKBOOTIMG) \
		$(recovery_ramdisk_device) \
		$(recovery_kernel)
	$(hide) python $(MKELF) $(MKELF_ARGS) -o $@ $(PRODUCT_OUT)/kernel@0x80208000 $(PRODUCT_OUT)/ramdisk-recovery.img@0x81900000,ramdisk $(DEVICE_RPMBIN)@0x00020000,rpm $(DEVICE_CMDLINE)@cmdline
	$(hide) $(call assert-max-image-size,$@,$(BOARD_RECOVERYIMAGE_PARTITION_SIZE))
