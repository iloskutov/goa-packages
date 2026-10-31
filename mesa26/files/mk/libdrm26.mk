#
# libdrm for Mesa 26: the sources of the stock 'libdrm' port with a dispatcher
# of its own and the back end for the panthor GPU driver (Gpu session).
#
# The stock libdrm.lib.so cannot be extended from another repository: its
# dispatcher (ioctl_dispatch.cc) is a closed switch over etnaviv and lima.
# Hence a second library; it must not be loaded together with libdrm.lib.so
# (same symbols).
#
SHARED_LIB := yes
LIBS       := libc

STOCK_LIBDRM_DIR := glue/libdrm
LIBDRM26_DIR     := glue/libdrm

# shadows libdrm_macros.h
INC_DIR += $(STOCK_LIBDRM_DIR)/include $(STOCK_LIBDRM_DIR)

INC_DIR += lib/libdrm lib/libdrm/include lib/libdrm/include/drm


# drm-uapi/panthor_drm.h of Mesa 26
INC_DIR += lib/mesa26/include

SRC_C := xf86drm.c \
         xf86drmHash.c \
         xf86drmMode.c \
         xf86drmRandom.c \
         xf86drmSL.c \
         dummies.c

SRC_CC := ioctl_dispatch.cc ioctl_panthor.cc

INC_DIR += glue/libdrm
CC_OPT := -D__GENODE__ -DHAVE_LIBDRM_ATOMIC_PRIMITIVES=1 -DHAVE_SYS_SYSCTL_H=1

#
# 'ioctl' calls are renamed to 'genode_ioctl' (as in the stock libdrm), so
# that no libc plugin is needed.
#
CC_C_OPT += -Dioctl=genode_ioctl

vpath %.c  lib/libdrm
vpath %.c  $(STOCK_LIBDRM_DIR)
vpath %.cc $(LIBDRM26_DIR)

