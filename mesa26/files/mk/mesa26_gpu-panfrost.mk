#
# EGL back end for the panfrost driver (Mali-G710 through the panthor GPU
# driver), loaded by egl26 under the name 'mesa26_gpu.lib.so' (ROM route)
#
SHARED_LIB := yes
LIBS       := libc egl26 mesa26 libdrm26

include mk/mesa26_gpu.inc

INC_DIR += glue/libdrm

SRC_C := platform_panfrost.c

vpath %.c $(MESA26_DIR)/panfrost
