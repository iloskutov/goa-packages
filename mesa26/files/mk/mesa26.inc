#
# mesa26.lib.so: Mesa core, GL/GLES entry points, the GLSL and NIR compilers,
# gallium auxiliary code, the DRI frontend and the softpipe driver.
# Corresponds to upstream's libgallium-26.2.6.so.
#

SHARED_LIB := yes
LIBS       += libc libm stdcxx zlib

include mk/mesa26-common.inc
include mk/mesa26_list-core.inc

#
# Gallium drivers built into the library (upstream: per-target defines of
# the pipe loader and of the DRI target). MESA26_DRIVER_DEFS is extended by
# the spec file that adds a hardware driver.
#
MESA26_DRIVER_DEFS += -DGALLIUM_SOFTPIPE

CC_OPT_gallium/auxiliary/pipe-loader/pipe_loader     += $(MESA26_DRIVER_DEFS)
CC_OPT_gallium/auxiliary/pipe-loader/pipe_loader_sw  += $(MESA26_DRIVER_DEFS)
CC_OPT_gallium/auxiliary/pipe-loader/pipe_loader_drm += $(MESA26_DRIVER_DEFS)
CC_OPT_gallium/targets/dri/dri_target                += $(MESA26_DRIVER_DEFS)

# the DRM part of the pipe loader needs libdrm
ifeq ($(filter libdrm26,$(LIBS)),)
SRC_C := $(filter-out gallium/auxiliary/pipe-loader/pipe_loader_drm.c,$(SRC_C))
endif

vpath %.cc $(MESA26_DIR)

