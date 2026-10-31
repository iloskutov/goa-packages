/*
 * \brief  Interface between the EGL platform "genode" of Mesa 26 and its
 *         back ends (mesa26_gpu-<driver>.lib.so)
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * The platform code (egl/platform.c, part of egl26.lib.so) loads the library
 * 'mesa26_gpu.lib.so' and calls 'dri2_initialize_genode_backend'. Which
 * driver that is, is decided by the ROM route of that name:
 * 'mesa26_gpu-softpipe.lib.so' or 'mesa26_gpu-panfrost.lib.so'.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _MESA26__GENODE_EGL_H_
#define _MESA26__GENODE_EGL_H_

#include <egl_dri2.h>

/* name the EGL platform asks the dynamic linker for */
#define GENODE_EGL_BACKEND_LIB "mesa26_gpu.lib.so"

/**
 * Entry of a back end: create the DRI screen and the EGL configurations
 */
EGLBoolean dri2_initialize_genode_backend(_EGLDisplay *disp);

/*
 * Helpers of the platform code for the back ends
 */

_EGLSurface *dri2_genode_create_window_surface(_EGLDisplay *disp, _EGLConfig *conf,
                                               void *native_window,
                                               const EGLint *attrib_list);

_EGLSurface *dri2_genode_create_pixmap_surface(_EGLDisplay *disp, _EGLConfig *conf,
                                               void *native_pixmap,
                                               const EGLint *attrib_list);

EGLBoolean dri2_genode_destroy_surface(_EGLDisplay *disp, _EGLSurface *surf);

EGLBoolean dri2_genode_swap_interval(_EGLDisplay *disp, _EGLSurface *surf,
                                     EGLint interval);

/**
 * Add the EGL configurations whose color buffer has the layout of Genode's
 * pixel buffers (32 bit, blue in the lowest byte)
 *
 * \return number of configurations added
 */
unsigned dri2_genode_add_configs(_EGLDisplay *disp);

/**
 * Copy 'h' rows of 'bytes' bytes
 */
void dri2_genode_copy_rows(char const *src, int src_stride,
                           char *dst, int dst_stride, int bytes, int h);

#endif /* _MESA26__GENODE_EGL_H_ */
