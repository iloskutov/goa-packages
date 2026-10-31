#
# EGL back end for software rendering, loaded by egl26 under the name
# 'mesa26_gpu.lib.so' (ROM route)
#
SHARED_LIB := yes
LIBS       := libc egl26 mesa26

include mk/mesa26_gpu.inc

SRC_C := platform_softpipe.c

vpath %.c $(MESA26_DIR)/softpipe
