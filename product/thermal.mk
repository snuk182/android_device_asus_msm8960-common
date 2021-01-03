# Thermal configuration
PRODUCT_COPY_FILES += \
    $(COMMON_PATH)/configs/thermanager.xml:$(TARGET_COPY_OUT_VENDOR)/etc/thermanager.xml

# Thermal management packages
PRODUCT_PACKAGES += \
    thermanager
