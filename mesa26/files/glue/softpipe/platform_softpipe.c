/*
 * \brief  EGL back end of Mesa 26 for software rendering (softpipe)
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * Counterpart of repos/libports/src/lib/mesa/softpipe/platform_softpipe.c.
 * The DRI software screen presents through the loader's 'putImage' and reads
 * the front buffer back through 'getImage'; both work on the pixel buffer of
 * the native window.
 */

/*
 * SPDX-License-Identifier: MIT
 */

/* Mesa */
#include <egl_dri2.h>
#include <egldevice.h>
#include <dri_util.h>

/* libc */
#include <string.h>

/* local */
#include <genode_egl.h>

enum { BYTES_PER_PIXEL = 4 };


static void swrast_get_drawable_info(struct dri_drawable *draw,
                                     int *x, int *y, int *w, int *h,
                                     void *loader_private)
{
	struct dri2_egl_surface  *dri2_surf = loader_private;
	struct Genode_egl_window *window    = dri2_surf->g_win;

	(void)draw;

	*x = 0;
	*y = 0;
	*w = window->width;
	*h = window->height;

	dri2_surf->base.Width  = window->width;
	dri2_surf->base.Height = window->height;
}


static void swrast_put_image(struct dri_drawable *draw, int op,
                             int x, int y, int w, int h,
                             char *data, void *loader_private)
{
	struct dri2_egl_surface  *dri2_surf = loader_private;
	struct Genode_egl_window *window    = dri2_surf->g_win;

	int const dst_stride = dri2_surf->base.Width*BYTES_PER_PIXEL;
	int const src_stride = w*BYTES_PER_PIXEL;
	int const x_offset   = x*BYTES_PER_PIXEL;
	int       copy_width = src_stride;

	(void)draw; (void)op;

	/* drivers are allowed to submit out-of-bounds requests */
	if (copy_width > dst_stride - x_offset)
		copy_width = dst_stride - x_offset;
	if (h > dri2_surf->base.Height - y)
		h = dri2_surf->base.Height - y;
	if (copy_width <= 0 || h <= 0)
		return;

	dri2_genode_copy_rows(data, src_stride,
	                      (char *)window->addr + y*dst_stride + x_offset,
	                      dst_stride, copy_width, h);
}


static void swrast_get_image(struct dri_drawable *read,
                             int x, int y, int w, int h,
                             char *data, void *loader_private)
{
	struct dri2_egl_surface  *dri2_surf = loader_private;
	struct Genode_egl_window *window    = dri2_surf->g_win;

	int const src_stride = dri2_surf->base.Width*BYTES_PER_PIXEL;
	int const dst_stride = w*BYTES_PER_PIXEL;
	int const x_offset   = x*BYTES_PER_PIXEL;
	int       copy_width = dst_stride;

	(void)read;

	if (copy_width > src_stride - x_offset)
		copy_width = src_stride - x_offset;
	if (h > dri2_surf->base.Height - y)
		h = dri2_surf->base.Height - y;
	if (copy_width <= 0 || h <= 0)
		return;

	dri2_genode_copy_rows((char const *)window->addr + y*src_stride + x_offset,
	                      src_stride, data, dst_stride, copy_width, h);
}


static const __DRIswrastLoaderExtension swrast_loader_extension = {
	.base = { __DRI_SWRAST_LOADER, 1 },

	.getDrawableInfo = swrast_get_drawable_info,
	.putImage        = swrast_put_image,
	.getImage        = swrast_get_image,
};


static const __DRIextension *swrast_loader_extensions[] = {
	&swrast_loader_extension.base,
	&image_lookup_extension.base,
	NULL,
};


static EGLBoolean swrast_swap_buffers(_EGLDisplay *disp, _EGLSurface *draw)
{
	struct dri2_egl_surface *dri2_surf = dri2_egl_surface(draw);

	(void)disp;

	driSwapBuffers(dri2_surf->dri_drawable);
	return EGL_TRUE;
}


static const struct dri2_egl_display_vtbl display_vtbl = {
	.create_window_surface = dri2_genode_create_window_surface,
	.create_pixmap_surface = dri2_genode_create_pixmap_surface,
	.destroy_surface       = dri2_genode_destroy_surface,
	.create_image          = dri2_create_image_khr,
	.swap_interval         = dri2_genode_swap_interval,
	.swap_buffers          = swrast_swap_buffers,
	.get_dri_drawable      = dri2_surface_get_dri_drawable,
};


EGLBoolean dri2_initialize_genode_backend(_EGLDisplay *disp)
{
	struct dri2_egl_display *dri2_dpy = dri2_egl_display(disp);

	/* the first EGL device is the software device */
	disp->Device = _eglFindDevice(-1, true);

	dri2_dpy->fd_render_gpu  = -1;
	dri2_dpy->fd_display_gpu = -1;
	dri2_dpy->driver_name    = strdup("swrast");
	if (!dri2_dpy->driver_name)
		return _eglError(EGL_BAD_ALLOC, "genode: softpipe");

	/* sets 'swrast' and 'swrast_not_kms', which select the screen type */
	dri2_detect_swrast_kopper(disp);

	dri2_dpy->loader_extensions = swrast_loader_extensions;

	if (!dri2_create_screen(disp))
		return _eglError(EGL_NOT_INITIALIZED, "genode: failed to create screen");

	dri2_setup_screen(disp);
	dri2_setup_swap_interval(disp, 1);

	if (!dri2_genode_add_configs(disp))
		return _eglError(EGL_NOT_INITIALIZED, "genode: no usable configuration");

	/* filled last, no virtual function is called during initialization */
	dri2_dpy->vtbl = &display_vtbl;

	return EGL_TRUE;
}
