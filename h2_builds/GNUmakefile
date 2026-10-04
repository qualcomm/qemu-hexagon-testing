# Build the h2 kernel and booter for each QEMU Hexagon machine.  See README.md.
#
#   make                       # build every machine in configs/
#   make qcs6490-cdsp          # build one
#   make H2_SRC=~/src/hexagon-hypervisor ...   # build from a local h2 checkout
#   make package               # build/dist/* and build/h2-builds.tar.gz
#   make list

# The patches in patches/ apply on top of this commit of the upstream h2.
H2_REPO ?= https://github.com/qualcomm/hexagon-hypervisor
H2_REF  ?= 23d5bde4313c1e63ba64dd5d7f542b1055188ef8
H2_SRC  ?= $(CURDIR)/build/h2-src
export H2_SRC H2_REPO H2_REF

MACHINES := $(basename $(notdir $(wildcard configs/*.mk)))

all: $(MACHINES)

# Fetch h2 once, before any (possibly parallel) machine build needs it.
$(H2_SRC)/makefile:
	scripts/fetch_h2.sh

$(MACHINES): | $(H2_SRC)/makefile
	scripts/build_h2.sh $@

check: $(addprefix check-,$(MACHINES))
check-%:
	scripts/check_layout.sh $*

package: all
	scripts/package.sh

list:
	@printf '%s\n' $(MACHINES)

clean:
	rm -rf build

.PHONY: all package check list clean $(MACHINES)
