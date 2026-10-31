#
# egl26.lib.so: Mesa 26's EGL with the platform "genode". Corresponds to
# upstream's libEGL.so, which calls the DRI frontend in libgallium
# (mesa26.lib.so) directly.
#
SHARED_LIB := yes
LIBS       := libc mesa26

include mk/mesa26-common.inc

INC_DIR += $(MESA_SRC_DIR)/src/egl/main \
           $(MESA_SRC_DIR)/src/egl/drivers/dri2 \
           $(MESA_SRC_DIR)/src/gallium/frontends/dri \
           $(MESA_SRC_DIR)/src/loader \
           $(MESA_SRC_DIR)/src/mesa \
           $(MESA_SRC_DIR)/src/mesa/glapi/glapi \
           $(MESA_GEN_DIR)/src/mesa/glapi/glapi/gen \
           $(MESA_GEN_DIR)/src/mesa \
           $(MESA_GEN_DIR)/src/util

SRC_C := egl/main/eglapi.c \
         egl/main/eglarray.c \
         egl/main/eglconfig.c \
         egl/main/eglconfigdebug.c \
         egl/main/eglcontext.c \
         egl/main/eglcurrent.c \
         egl/main/egldevice.c \
         egl/main/egldisplay.c \
         egl/main/eglglobals.c \
         egl/main/eglimage.c \
         egl/main/egllog.c \
         egl/main/eglsurface.c \
         egl/main/eglsync.c \
         egl/drivers/dri2/egl_dri2.c \
         egl/drivers/dri2/platform_device.c \
         egl/drivers/dri2/platform_surfaceless.c \
         platform.c

CC_OPT += -D_EGL_NATIVE_PLATFORM=_EGL_PLATFORM_GENODE

vpath platform.c $(MESA26_DIR)/egl
