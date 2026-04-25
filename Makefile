BUILD_DIR := $(CURDIR)/build

include common.mk

OBJ_DIR = $(BUILD_DIR)/obj/$(BUILD_TYPE)
MODULE_OUT = $(BUILD_DIR)/module/$(BUILD_TYPE)
ZIP_DIR = $(BUILD_DIR)/out

ZKSU_VERSION = $(VER_NAME)-$(VER_CODE)-$(COMMIT_HASH)-$(BUILD_TYPE)

ZIP_NAME = $(MODULE_NAME)-$(VER_NAME)-$(VER_CODE)-$(COMMIT_HASH)-$(BUILD_TYPE).zip
ZIP_FILE = $(ZIP_DIR)/$(ZIP_NAME)

ifeq ($(TERMUX_VERSION),)
	ADB_CMD := adb push $(ZIP_FILE) /data/local/tmp && adb shell 
	INSTALL_PATH := /data/local/tmp/$(ZIP_NAME)
	REBOOT_CMD := adb reboot
else
	INSTALL_PATH := $(ZIP_FILE)
	REBOOT_CMD := su -c reboot
endif

LOADER_DONE = $(OBJ_DIR)/loader/.done
ZYGISKD_DONE = $(OBJ_DIR)/zygiskd/.done
MODULE_DONE = $(BUILD_DIR)/module-$(BUILD_TYPE).done

LOADER_INPUTS = common.mk loader/Makefile \
        $(shell find loader/src -type f | sort)

ZYGISKD_INPUTS = common.mk zygiskd/Makefile \
        $(shell find zygiskd/src -type f | sort)

MODULE_INPUTS = scripts/sign.py \
        $(shell find module/src -type f | sort) \
        $(shell find webroot -type f | sort) \
        $(wildcard module/private_key module/public_key)

.PHONY: debug release build clean                                         \
        installKsu installMagisk installAPatch                            \
        installKsuAndReboot installMagiskAndReboot installAPatchAndReboot

debug:
	$(MAKE) BUILD_TYPE=debug BUILD_DIR=$(BUILD_DIR) build

release:
	$(MAKE) BUILD_TYPE=release BUILD_DIR=$(BUILD_DIR) build

all: debug release

build: $(ZIP_FILE)

$(LOADER_DONE): $(LOADER_INPUTS)
	$(MAKE) -C loader BUILD_TYPE=$(BUILD_TYPE) BUILD_DIR=$(BUILD_DIR) ZKSU_VERSION=$(ZKSU_VERSION)
	@mkdir -p $(dir $@)
	@touch $@

$(ZYGISKD_DONE): $(ZYGISKD_INPUTS)
	$(MAKE) -C zygiskd BUILD_TYPE=$(BUILD_TYPE) BUILD_DIR=$(BUILD_DIR) ZKSU_VERSION=$(ZKSU_VERSION)
	@mkdir -p $(dir $@)
	@touch $@

$(MODULE_DONE): $(LOADER_DONE) $(ZYGISKD_DONE) $(MODULE_INPUTS)

	@rm -rf $(MODULE_OUT)
	@mkdir -p $(MODULE_OUT)/META-INF/com/google/android

	@echo "Copying META-INF files..."
	@cp module/src/META-INF/com/google/android/update-binary  \
	   module/src/META-INF/com/google/android/updater-script \
	   $(MODULE_OUT)/META-INF/com/google/android/


	@echo "Copying module files..."
	@cp module/src/verify.sh module/src/sepolicy.rule module/src/rezygisk.sh $(MODULE_OUT)/

	@echo "Customizing module.prop..."
	@sed -e 's/$${moduleId}/$(MODULE_ID)/g'                                             \
	    -e 's/$${moduleName}/$(MODULE_NAME)/g'                                          \
	    -e 's/$${versionName}/$(VER_NAME) ($(VER_CODE)-$(COMMIT_HASH)-$(BUILD_TYPE))/g' \
	    -e 's/$${versionCode}/$(VER_CODE)/g'                                            \
	    module/src/module.prop > $(MODULE_OUT)/module.prop

	@echo "Customizing scripts..."
	@for script in customize.sh post-fs-data.sh service.sh uninstall.sh; do \
		sed -e 's/@DEBUG@/$(if $(filter debug,$(BUILD_TYPE)),true,false)/g' \
		    -e 's/@MIN_APATCH_VERSION@/$(MIN_APATCH_VERSION)/g'             \
		    -e 's/@MIN_KSU_VERSION@/$(MIN_KSU_VERSION)/g'                   \
		    -e 's/@MIN_KSUD_VERSION@/$(MIN_KSUD_VERSION)/g'                 \
		    -e 's/@MIN_MAGISK_VERSION@/$(MIN_MAGISK_VERSION)/g'             \
		    module/src/$$script > $(MODULE_OUT)/$$script;                   \
	done

	@echo "Copying binaries..."
	@for arch in $(ARCHS); do                                                                                  \
		mkdir -p $(MODULE_OUT)/bin/$$arch $(MODULE_OUT)/lib/$$arch;                                            \
		cp $(OBJ_DIR)/zygiskd/$$arch/zygiskd $(MODULE_OUT)/bin/$$arch/zygiskd;                                 \
		cp $(OBJ_DIR)/loader/$$arch/stripped/libzygisk.so $(MODULE_OUT)/lib/$$arch/libzygisk.so;               \
		cp $(OBJ_DIR)/loader/$$arch/stripped/libzygisk_ptrace.so $(MODULE_OUT)/lib/$$arch/libzygisk_ptrace.so; \
	done

	@echo "Copying webroot..."
	@cp -r webroot $(MODULE_OUT)/webroot

	@if [ -f module/private_key ]; then                                             \
		echo "Signing module...";                                                   \
		python3 scripts/sign.py $(MODULE_OUT) module/private_key module/public_key; \
	else                                                                            \
	    echo "No private key found, skipping signing...";                           \
		python3 scripts/sign.py --no-sign $(MODULE_OUT);                            \
	fi

	@mkdir -p $(dir $@)
	@touch $@

$(ZIP_FILE): $(MODULE_DONE)
	@mkdir -p $(ZIP_DIR)
	@rm -f $@

	@echo "Creating ZIP file..."
	@cd $(MODULE_OUT) && zip -r9 $@ . -x '*.DS_Store' > /dev/null

installKsu: build
	$(ADB_CMD)su -c '/data/adb/ksu/bin/ksud module install $(INSTALL_PATH)'

installMagisk: build
	$(ADB_CMD)su -M -c "magisk --install-module $(INSTALL_PATH)"

installAPatch: build
	$(ADB_CMD)su -c "/data/adb/apd module install $(INSTALL_PATH)"

installKsuAndReboot: installKsu
	$(REBOOT_CMD)

installMagiskAndReboot: installMagisk
	$(REBOOT_CMD)

installAPatchAndReboot: installAPatch
	$(REBOOT_CMD)

clean:
	rm -rf $(BUILD_DIR)
	$(MAKE) -C loader clean BUILD_DIR=$(BUILD_DIR)
	$(MAKE) -C zygiskd clean BUILD_DIR=$(BUILD_DIR)
