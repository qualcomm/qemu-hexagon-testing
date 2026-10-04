# Build the h2 hypervisor (cloned from github unless H2_SRC points at a checkout)
# and the h2 guest test programs.  See README.md.
#
#   make                 # build h2 + all tests into build/v$(ARCHV)/
#   make check           # run all tests on hexagon-sim
#   make check-qemu      # run all tests on qemu-system-hexagon
#   make ARCHV=68 ...    # target v68 instead of v73 (also builds h2 for v68)
#   make V=1             # show full command lines

ARCHV       ?= 73
SDK         ?= /opt/Hexagon_SDK/6.4.0.2
TOOLS       ?= $(SDK)/tools/HEXAGON_Tools/19.0.04/Tools
H2_REPO     ?= https://github.com/qualcomm/hexagon-hypervisor
H2_REF      ?= master
H2_SRC      ?= $(CURDIR)/build/h2-src
H2_INSTALL  := $(H2_SRC)/artifacts/v$(ARCHV)/opt/install
BUILD       := $(CURDIR)/build/v$(ARCHV)

Q := $(if $(V),,@)

export PATH := $(TOOLS)/bin:$(PATH)

CC      := hexagon-clang
CFLAGS  := -O2 -mv$(ARCHV) -G0 -Wall -Werror -DARCHV=$(ARCHV) -Iinclude \
           -I$(H2_INSTALL)/include -mhvx -mhvx-length=128B -mhmx $(EXTRA_CFLAGS)
GUEST_START ?= 0x02000000
LDFLAGS := -mv$(ARCHV) -moslib=h2 -L$(H2_INSTALL)/lib -Qunused-arguments \
           -Wl,-e=start -Wl,--section-start=.start=$(GUEST_START)

TESTS := $(basename $(notdir $(wildcard src/*.c)))
EXES  := $(addprefix $(BUILD)/,$(TESTS))

all: $(EXES)

$(H2_INSTALL)/bin/booter:
	scripts/build_h2.sh "$(H2_REPO)" "$(H2_REF)" "$(H2_SRC)" $(ARCHV)

$(BUILD)/%.o: src/%.c include/h2t.h | $(H2_INSTALL)/bin/booter
	@mkdir -p $(BUILD)
	$(Q)$(if $(Q),echo "  CC   $(notdir $<)")
	$(Q)$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%: $(BUILD)/%.o
	$(Q)$(if $(Q),echo "  LINK $(notdir $@)")
	$(Q)$(CC) $(LDFLAGS) -o $@ $<

.SECONDARY:

check: all
	@ARCHV=$(ARCHV) TOOLS=$(TOOLS) H2_INSTALL=$(H2_INSTALL) \
	  scripts/run_tests.sh sim $(EXES)

check-qemu: all
	@ARCHV=$(ARCHV) SDK=$(SDK) H2_INSTALL=$(H2_INSTALL) \
	  scripts/run_tests.sh qemu $(EXES)

clean:
	rm -rf $(BUILD)

.PHONY: all check check-qemu clean
