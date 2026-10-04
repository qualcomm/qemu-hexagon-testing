# Build and run the QuRT functional tests using the headers/libraries shipped
# in the Hexagon SDK.  See README.md.
#
#   make                 # build all tests into build/$(ARCH)/
#   make check           # run all tests on hexagon-sim
#   make check-qemu      # run all tests on qemu-system-hexagon
#   make ARCH=v68 ...    # target v68 instead of v73
#   make V=1             # show full command lines

ARCH    ?= v73
SDK     ?= /opt/Hexagon_SDK/6.4.0.2
TOOLS   ?= $(SDK)/tools/HEXAGON_Tools/19.0.04/Tools
QURT    := $(SDK)/rtos/qurt/compute$(ARCH)
LIBDIR  := $(TOOLS)/target/hexagon/lib/$(ARCH)/G0
BUILD   := $(CURDIR)/build/$(ARCH)
Q       := $(if $(V),,@)

CC      := $(TOOLS)/bin/hexagon-clang
CFLAGS  := -O2 -m$(ARCH) -G0 -Wall -Werror -Iinclude \
           -I$(QURT)/include -I$(QURT)/include/qurt -I$(QURT)/include/posix \
           -mhvx -mhvx-length=128B -mhmx $(EXTRA_CFLAGS)
# Link line taken from $(SDK)/build/cmake/hexagon_toolchain.cmake (EXE_QURT_LD_FLAGS)
LDFLAGS := -m$(ARCH) -nodefaultlibs -nostdlib \
           -Wl,--section-start -Wl,.interp=0x23000000 -Wl,--dynamic-linker= \
           -Wl,--force-dynamic -Wl,-E -Wl,-z -Wl,muldefs -Wl,--whole-archive
QURT_LIBS := $(LIBDIR)/init.o $(QURT)/lib/crt1.o $(QURT)/lib/debugmon.o \
             $(QURT)/lib/libqurt.a $(LIBDIR)/libc.a $(LIBDIR)/libqcc.a \
             $(LIBDIR)/libhexagon.a $(QURT)/lib/libqurtcfs.a \
             $(QURT)/lib/libtimer_island.a $(QURT)/lib/libtimer_main.a \
             $(QURT)/lib/libposix.a
QURT_END  := $(LIBDIR)/fini.o

TESTS := $(basename $(notdir $(wildcard src/*.c)))
EXES  := $(addprefix $(BUILD)/,$(TESTS))

all: $(EXES)

$(BUILD)/%.o: src/%.c include/qt.h
	@mkdir -p $(BUILD)
	$(Q)$(if $(Q),echo "  CC   $(notdir $<)")
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%: $(BUILD)/%.o
	$(Q)$(if $(Q),echo "  LINK $(notdir $@)")
	$(Q)$(CC) $(LDFLAGS) -o $@ -Wl,--start-group $(QURT_LIBS) $< $(QURT_END) -Wl,--end-group >$@.link.log 2>&1; rc=$$?; grep -v "weak symbol" $@.link.log; test $$rc -eq 0

.SECONDARY:

check: all
	@ARCH=$(ARCH) SDK=$(SDK) TOOLS=$(TOOLS) scripts/run_tests.sh sim $(EXES)

check-qemu: all
	@ARCH=$(ARCH) SDK=$(SDK) TOOLS=$(TOOLS) scripts/run_tests.sh qemu $(EXES)

clean:
	rm -rf $(BUILD)

.PHONY: all check check-qemu clean
