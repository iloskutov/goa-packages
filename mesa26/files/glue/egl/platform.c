/*
 * \brief  EGL platform "genode" for Mesa 26, driver-independent part
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * Counterpart of repos/libports/src/lib/mesa/egl/platform.c, written anew
 * for Mesa 26: the DRI interface is no longer a set of extension tables
 * obtained from a loaded driver but a set of functions of the Mesa library
 * (mesa26.lib.so), which EGL calls directly.
 *
 * A native window is a 'Genode_egl_window' (EGL/eglplatform.h): a pixel
 * buffer of 32 bit per pixel that belongs to the application. The software
 * back end presents into it with the loader's 'putImage', a hardware back
 * end renders into a linear image and copies it at swap.
 */

/*
 * SPDX-License-Identifier: MIT
 */

/* Mesa */
#include <egl_dri2.h>
#include <dri_util.h>
#include <util/format/u_formats.h>

/* libc */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* local */
#include <genode_egl.h>


void dri2_genode_copy_rows(char const *src, int src_stride,
                           char *dst, int dst_stride, int bytes, int h)
{
	for (; h > 0; h--) {
		memcpy(dst, src, bytes);
		src += src_stride;
		dst += dst_stride;
	}
}


EGLBoolean dri2_genode_swap_interval(_EGLDisplay *disp, _EGLSurface *surf,
                                     EGLint interval)
{
	(void)disp;

	if (interval > surf->Config->MaxSwapInterval)
		interval = surf->Config->MaxSwapInterval;
	else if (interval < surf->Config->MinSwapInterval)
		interval = surf->Config->MinSwapInterval;

	surf->SwapInterval = interval;

	return EGL_TRUE;
}


unsigned dri2_genode_add_configs(_EGLDisplay *disp)
{
	struct dri2_egl_display *dri2_dpy = dri2_egl_display(disp);
	unsigned count = 0;

	for (unsigned i = 0; dri2_dpy->driver_configs[i]; i++) {

		struct gl_config const *gl_config =
			(struct gl_config const *)dri2_dpy->driver_configs[i];

		/* layout of a Genode pixel buffer, with and without alpha */
		if (gl_config->color_format != PIPE_FORMAT_BGRX8888_UNORM &&
		    gl_config->color_format != PIPE_FORMAT_BGRA8888_UNORM)
			continue;

		if (dri2_add_config(disp, dri2_dpy->driver_configs[i],
		                    EGL_WINDOW_BIT | EGL_PIXMAP_BIT | EGL_PBUFFER_BIT,
		                    NULL))
			count++;
	}

	return count;
}


static _EGLSurface *_create_surface(_EGLDisplay *disp, _EGLConfig *conf,
                                    void *native_window,
                                    const EGLint *attrib_list,
                                    enum Surface_type type)
{
	struct dri2_egl_display *dri2_dpy  = dri2_egl_display(disp);
	struct dri2_egl_config  *dri2_conf = dri2_egl_config(conf);
	struct Genode_egl_window *window   = native_window;
	struct dri2_egl_surface *dri2_surf;
	const struct dri_config *config;

	if (!window) {
		_eglError(EGL_BAD_NATIVE_WINDOW, "genode: no native window");
		return NULL;
	}

	dri2_surf = calloc(1, sizeof *dri2_surf);
	if (!dri2_surf) {
		_eglError(EGL_BAD_ALLOC, "genode: create surface");
		return NULL;
	}

	/*
	 * A pixmap is handled like a window (rendering into a back buffer that
	 * is copied at swap), as in the stock port. The native pixmap structure
	 * is copied because the application may release it.
	 */
	window->type = type;
	if (type == PIXMAP) {
		struct Genode_egl_window *copy = calloc(1, sizeof *copy);
		if (!copy)
			goto cleanup_surf;
		*copy  = *window;
		window = copy;
	}

	if (!dri2_init_surface(&dri2_surf->base, disp, EGL_WINDOW_BIT, conf,
	                       attrib_list, false, native_window))
		goto cleanup_window;

	dri2_surf->g_win       = window;
	dri2_surf->base.Width  = window->width;
	dri2_surf->base.Height = window->height;
	dri2_surf->visual      = PIPE_FORMAT_BGRX8888_UNORM;

	config = dri2_get_dri_config(dri2_conf, EGL_WINDOW_BIT,
	                             dri2_surf->base.GLColorspace);
	if (!config) {
		_eglError(EGL_BAD_MATCH, "genode: unsupported surface type / colorspace");
		goto cleanup_window;
	}

	/* a hardware driver renders into an image that is copied at swap */
	if (!dri2_dpy->swrast) {
		dri2_surf->back_image =
			dri_create_image(dri2_dpy->dri_screen_render_gpu,
			                 dri2_surf->base.Width, dri2_surf->base.Height,
			                 PIPE_FORMAT_BGRX8888_UNORM, NULL, 0,
			                 __DRI_IMAGE_USE_LINEAR | __DRI_IMAGE_USE_BACKBUFFER,
			                 NULL);
		if (!dri2_surf->back_image) {
			_eglError(EGL_BAD_ALLOC, "genode: back image");
			goto cleanup_window;
		}
	}

	if (!dri2_create_drawable(dri2_dpy, config, dri2_surf, dri2_surf))
		goto cleanup_image;

	dri2_genode_swap_interval(disp, &dri2_surf->base,
	                          dri2_dpy->default_swap_interval);

	return &dri2_surf->base;

cleanup_image:
	if (dri2_surf->back_image)
		dri2_destroy_image(dri2_surf->back_image);
cleanup_window:
	if (type == PIXMAP)
		free(window);
cleanup_surf:
	free(dri2_surf);
	return NULL;
}


_EGLSurface *dri2_genode_create_window_surface(_EGLDisplay *disp, _EGLConfig *conf,
                                               void *native_window,
                                               const EGLint *attrib_list)
{
	return _create_surface(disp, conf, native_window, attrib_list, WINDOW);
}


_EGLSurface *dri2_genode_create_pixmap_surface(_EGLDisplay *disp, _EGLConfig *conf,
                                               void *native_pixmap,
                                               const EGLint *attrib_list)
{
	return _create_surface(disp, conf, native_pixmap, attrib_list, PIXMAP);
}


EGLBoolean dri2_genode_destroy_surface(_EGLDisplay *disp, _EGLSurface *surf)
{
	struct dri2_egl_surface  *dri2_surf = dri2_egl_surface(surf);
	struct Genode_egl_window *window    = dri2_surf->g_win;

	(void)disp;

	driDestroyDrawable(dri2_surf->dri_drawable);

	if (dri2_surf->back_image)
		dri2_destroy_image(dri2_surf->back_image);

	if (window->type == PIXMAP)
		free(window);

	dri2_fini_surface(surf);
	free(dri2_surf);

	return EGL_TRUE;
}


EGLBoolean dri2_initialize_genode(_EGLDisplay *disp)
{
	typedef EGLBoolean (*backend_init)(_EGLDisplay *);

	/*
	 * A Genode component has no program name unless it was started with
	 * arguments, and Mesa compares the name of the process without a check
	 * for NULL (driconf's application matching). The stock port injects a
	 * name into xmlconfig for the same reason.
	 */
	if (!getprogname())
		setprogname("mesa_app");

	void *handle = dlopen(GENODE_EGL_BACKEND_LIB, RTLD_LAZY);
	if (!handle) {
		fprintf(stderr, "Error: could not open EGL back end ('%s')\n",
		        GENODE_EGL_BACKEND_LIB);
		return EGL_FALSE;
	}

	backend_init init =
		(backend_init)dlsym(handle, "dri2_initialize_genode_backend");
	if (!init) {
		fprintf(stderr, "Error: '%s' has no 'dri2_initialize_genode_backend'\n",
		        GENODE_EGL_BACKEND_LIB);
		return EGL_FALSE;
	}

	return init(disp);
}
