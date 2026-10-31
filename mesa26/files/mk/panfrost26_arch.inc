#
# The panfrost sources that upstream compiles once per architecture version
# (-DPAN_ARCH=<n>, all symbols carry the version). The including file sets
# PAN_ARCH.
#
# v4-v9 (Midgard, Bifrost, first Valhall) submit through the job manager,
# v10 and later through the CSF firmware. The Mali-G710 is v10; the other
# versions are built because the screen code refers to all of them.
#
include mk/panfrost26-common.inc
include mk/mesa26_list-panfrost_arch.inc

CC_OPT += -DPAN_ARCH=$(PAN_ARCH)

ifneq ($(filter $(PAN_ARCH),4 5 6 7 9),)
SRC_C := $(filter-out gallium/drivers/panfrost/pan_csf.c,$(SRC_C)) \
         gallium/drivers/panfrost/pan_jm.c
endif

# v4 shares the format table of v5
ifeq ($(PAN_ARCH),4)
SRC_C := $(filter-out panfrost/lib/pan_format.c,$(SRC_C))
endif

