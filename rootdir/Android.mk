LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

# files that live under /system/etc/...

copy_from := \
	etc/dbus.conf \
	etc/hosts \
	etc/magd.conf \
	etc/rril/repository.txt \
	etc/ppp/chap-secrets \
	etc/ppp/chat-isp \
	etc/ppp/pap-secrets \

	
# export to /system/bin to make executable	
PRODUCT_COPY_FILES += \
	$(LOCAL_PATH)/etc/rril-repo.sh:system/bin/rril-repo.sh \
	$(LOCAL_PATH)/etc/stop_muxd:system/bin/stop_muxd \
	$(LOCAL_PATH)/etc/init.gprs-pppd:system/bin/init.gprs-pppd \
	$(LOCAL_PATH)/etc/ppp/ip-down:system/bin/ip-down \
	$(LOCAL_PATH)/etc/ppp/ip-up:system/bin/ip-up \
	$(LOCAL_PATH)/etc/ppp/stop_pppd:system/bin/stop_pppd 	

# select pppd config file based on RIL interface
ifeq ($(strip $(RIL_COM_INTERFACE)),spi)
PRODUCT_COPY_FILES += $(LOCAL_PATH)/etc/ppp/peers/gprs.ril-spi:system/etc/ppp/peers/gprs
else ifeq ($(strip $(RIL_COM_INTERFACE)),usb)
PRODUCT_COPY_FILES += $(LOCAL_PATH)/etc/ppp/peers/gprs.ril-usb:system/etc/ppp/peers/gprs
else ifeq ($(strip $(RIL_COM_INTERFACE)),uart)
PRODUCT_COPY_FILES += $(LOCAL_PATH)/etc/ppp/peers/gprs.ril-uart:system/etc/ppp/peers/gprs
else ifeq ($(strip $(RIL_COM_INTERFACE)),usb-emu)
PRODUCT_COPY_FILES +=$(LOCAL_PATH)/etc/ppp/peers/gprs-emu:system/etc/ppp/peers/gprs
endif		

ifeq ($(TARGET_PRODUCT),generic)
copy_from += etc/vold.fstab
endif

# the /system/etc/init.goldfish.sh is needed to enable emulator support
# in the system image. In theory, we don't need these for -user builds
# which are device-specific. However, these builds require at the moment
# to run the dex pre-optimization *in* the emulator. So keep the file until
# we are capable of running dex preopt on the host.
#
copy_from += etc/init.goldfish.sh

copy_to := $(addprefix $(TARGET_OUT)/,$(copy_from))
copy_from := $(addprefix $(LOCAL_PATH)/,$(copy_from))

$(copy_to) : PRIVATE_MODULE := system_etcdir
$(copy_to) : $(TARGET_OUT)/% : $(LOCAL_PATH)/% | $(ACP)
	$(transform-prebuilt-to-target)

ALL_PREBUILT += $(copy_to)


# files that live under /...

# Only copy init.rc if the target doesn't have its own.
ifneq ($(TARGET_PROVIDES_INIT_RC),true)
file := $(TARGET_ROOT_OUT)/init.rc
$(file) : $(LOCAL_PATH)/init.rc | $(ACP)
	$(transform-prebuilt-to-target)
ALL_PREBUILT += $(file)
$(INSTALLED_RAMDISK_TARGET): $(file)
endif

file := $(TARGET_ROOT_OUT)/ueventd.rc
$(file) : $(LOCAL_PATH)/ueventd.rc | $(ACP)
	$(transform-prebuilt-to-target)
ALL_PREBUILT += $(file)
$(INSTALLED_RAMDISK_TARGET): $(file)

# Just like /system/etc/init.goldfish.sh, the /init.godlfish.rc is here
# to allow -user builds to properly run the dex pre-optimization pass in
# the emulator.
file := $(TARGET_ROOT_OUT)/init.goldfish.rc
$(file) : $(LOCAL_PATH)/etc/init.goldfish.rc | $(ACP)
	$(transform-prebuilt-to-target)
ALL_PREBUILT += $(file)
$(INSTALLED_RAMDISK_TARGET): $(file)

file := $(TARGET_ROOT_OUT)/ueventd.goldfish.rc
$(file) : $(LOCAL_PATH)/etc/ueventd.goldfish.rc | $(ACP)
	$(transform-prebuilt-to-target)
ALL_PREBUILT += $(file)
$(INSTALLED_RAMDISK_TARGET): $(file)

file := $(TARGET_ROOT_OUT)/ueventd.freescale.rc
$(file) : $(LOCAL_PATH)/etc/ueventd.freescale.rc | $(ACP)
	$(transform-prebuilt-to-target)
ALL_PREBUILT += $(file)
$(INSTALLED_RAMDISK_TARGET): $(file)

# create some directories (some are mount points)
DIRS := $(addprefix $(TARGET_ROOT_OUT)/, \
		sbin \
		dev \
		proc \
		sys \
		system \
		data \
	) \
	$(TARGET_OUT_DATA)

$(DIRS):
	@echo Directory: $@
	@mkdir -p $@

ALL_PREBUILT += $(DIRS)
