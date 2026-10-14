# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
# SPDX-License-Identifier: BSD-3-Clause-Clear

MINIVM_VERSION ?= 0.3.0
MINIVM_REPOSITORY ?= https://github.com/qualcomm/hexagonMVM.git
MINIVM_SOURCE_DIR ?= $(CURDIR)/source/hexagonMVM
MINIVM_BUILD_DIR ?= $(CURDIR)/build
MINIVM_ARCHIVE ?= $(CURDIR)/hexagon_minivm.tar.gz

MINIVM_CC ?= hexagon-unknown-none-elf-clang
MINIVM_LD ?= $(MINIVM_CC)
MINIVM_OBJCOPY ?= hexagon-unknown-none-elf-objcopy
MINIVM_SIM ?= hexagon-sim
MINIVM_TIMEOUT ?= 30
MINIVM_GUEST_ENTRY ?= 0xc0000000
MINIVM_ARCH_VERSION ?= 73

MINIVM_TESTS = test_mmu test_interrupts test_processors
MINIVM_ROOTFS = hexagon-unknown-linux-musl-rootfs

.PHONY: all fetch build archive test clean

all: archive

fetch:
	@mkdir -p $(dir $(MINIVM_SOURCE_DIR))
	@if test ! -d $(MINIVM_SOURCE_DIR)/.git; then \
		git clone --depth 1 --branch v$(MINIVM_VERSION) \
			$(MINIVM_REPOSITORY) $(MINIVM_SOURCE_DIR); \
	fi

build: fetch
	$(MAKE) -C $(MINIVM_SOURCE_DIR) \
		CC=$(MINIVM_CC) LD=$(MINIVM_LD) OBJCOPY=$(MINIVM_OBJCOPY) \
		ARCHV=$(MINIVM_ARCH_VERSION) GUEST_ENTRY=$(MINIVM_GUEST_ENTRY) \
		build_tests minivm minivm.bin

archive: build
	rm -rf $(MINIVM_BUILD_DIR)
	mkdir -p $(MINIVM_BUILD_DIR)/$(MINIVM_ROOTFS)/boot
	for test in $(MINIVM_TESTS); do \
		cp --sparse=always $(MINIVM_SOURCE_DIR)/tests_bin/$$test \
			$(MINIVM_BUILD_DIR)/$(MINIVM_ROOTFS)/boot/$$test; \
	done
	cp --sparse=always $(MINIVM_SOURCE_DIR)/minivm \
		$(MINIVM_BUILD_DIR)/$(MINIVM_ROOTFS)/boot/minivm
	cp --sparse=always $(MINIVM_SOURCE_DIR)/minivm.bin \
		$(MINIVM_BUILD_DIR)/$(MINIVM_ROOTFS)/boot/minivm.bin
	tar --sparse -czf $(MINIVM_ARCHIVE) -C $(MINIVM_BUILD_DIR) $(MINIVM_ROOTFS)

test: build
	for test in $(MINIVM_TESTS); do \
		timeout $(MINIVM_TIMEOUT) $(MINIVM_SIM) \
			--mv$(MINIVM_ARCH_VERSION) --quiet -- \
			$(MINIVM_SOURCE_DIR)/tests_bin/$$test; \
	done

clean:
	rm -rf $(MINIVM_BUILD_DIR) $(MINIVM_ARCHIVE)
	@if test -d $(MINIVM_SOURCE_DIR); then \
		$(MAKE) -C $(MINIVM_SOURCE_DIR) clean; \
	fi
