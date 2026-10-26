SYSTEMD_SONIC_GENERATOR = systemd-sonic-generator_1.0.0_$(CONFIGURED_ARCH).deb
$(SYSTEMD_SONIC_GENERATOR)_SRC_PATH = $(SRC_PATH)/systemd-sonic-generator
SONIC_DPKG_DEBS += $(SYSTEMD_SONIC_GENERATOR)
# Skip unit tests: ssg_main_smart_switch_npu segfaults in the build container (D15)
$(SYSTEMD_SONIC_GENERATOR)_DEB_BUILD_OPTIONS = nocheck
