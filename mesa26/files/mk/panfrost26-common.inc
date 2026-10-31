include mk/mesa26-common.inc
INC_DIR += lib/libdrm lib/libdrm/include lib/libdrm/include/drm

LIBS   += libc stdcxx
CC_OPT += -DHAVE_PANFROST -DHAVE_LIBDRM


#
# Descriptors the panfrost driver gets from the kernel interface (sync files,
# dma-bufs, the device) are numbers of the DRM back end, not of the libc.
# Their 'close' and 'lseek' (size of an imported dma-buf, pan_kmod.c) go to
# libdrm26, which passes other descriptors on.
#
CC_OPT += -Dclose=genode_drm_close -Dlseek=genode_drm_lseek

