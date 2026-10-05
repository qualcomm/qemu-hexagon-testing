# Build the h2 kernel and booter for each QEMU Hexagon machine.  See README.md.
#
#   make                       # build every machine in configs/
#   make qcs6490-cdsp          # build one
#   make H2_SRC=~/src/hexagon-hypervisor ...   # build from a local h2 checkout
#   make package               # build/dist/* and build/h2-builds.tar.gz
#   make list

H2_REPO ?= https://github.com/androm3da/hexagon-hypervisor
H2_REF  ?= bcain/qemu_boot
H2_SRC  ?= $(CURDIR)/build/h2-src
export H2_SRC H2_REPO H2_REF

MACHINES := $(basename $(notdir $(wildcard configs/*.mk)))

all: $(MACHINES)

# Fetch h2 once, before any (possibly parallel) machine build needs it.
$(H2_SRC)/makefile:
	git clone --depth 1 --branch $(H2_REF) $(H2_REPO) $(H2_SRC)

$(MACHINES): | $(H2_SRC)/makefile
	scripts/build_h2.sh $@

check: $(addprefix check-,$(MACHINES))
check-%:
	scripts/check_layout.sh $*

package: all
	scripts/package.sh

check-qemu:
	scripts/run_tests.sh

list:
	@printf '%s\n' $(MACHINES)

clean:
	rm -rf build

.PHONY: all package check check-qemu list clean $(MACHINES)
