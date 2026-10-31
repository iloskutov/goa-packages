/*
 * \brief  EGL back end of Mesa 26 for the panfrost driver (Mali CSF, panthor)
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * Counterpart of repos/libports/src/lib/mesa/lima/platform_lima.c, written
 * for Mesa 26. The driver renders into a linear image ('back_image' of the
 * surface, created by the platform code); at swap, the image is mapped and
 * copied into the pixel buffer of the native window. There is no zero-copy
 * path.
 *
 * The device is reached through libdrm26 and its panthor back end. Its
 * descriptor is the constant 'Libdrm26::PANTHOR_FD'; Mesa learns the driver
 * from DRM_IOCTL_VERSION (name "panthor"), for which the pipe loader has the
 * panfrost driver.
 *
 * The back image is a linear render target. With cached buffer objects
 * its mapping is cached memory, and the
 * map for reading at swap -- 'dri2_map_image', i.e. the driver's
 * 'panfrost_ptr_map' -- waits for the GPU and then invalidates the CPU
 * cache for the mapped range. The copy must therefore go through that map
 * and never through a pointer kept from an earlier frame.
 */

/*
 * SPDX-License-Identifier: MIT
 */

/* Mesa */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <egl_dri2.h>
#include <egldevice.h>
#include <dri_util.h>

/* libc */
#include <string.h>

/* local */
#include <genode_egl.h>
#include <libdrm26.h>

enum { BYTES_PER_PIXEL = 4 };


/*
 * Where the time of a swap goes: with the environment variable
 * MESA26_SWAP_STATS set, one line per 256 frames with the mean time of
 * the flush (submit), of the mapping (wait for the GPU and, with a cached
 * back image, the cache invalidation) and of the copy into the window's
 * buffer.
 */
static uint64_t swap_now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec*1000000 + (uint64_t)ts.tv_nsec/1000;
}


static void swap_stats(uint64_t t0, uint64_t t1, uint64_t t2, uint64_t t3)
{
	static int      enabled = -1;
	static uint64_t flush_us, map_us, copy_us, last_us;
	static unsigned frames;

	if (enabled < 0)
		enabled = getenv("MESA26_SWAP_STATS") != NULL;
	if (!enabled)
		return;

	flush_us += t1 - t0; map_us += t2 - t1; copy_us += t3 - t2;

	if (++frames < 256)
		return;

	printf("swap stats: per frame %u us in total: flush %u us, map %u us, "
	       "copy and unmap %u us\n",
	       (unsigned)((t3 - last_us)/frames), (unsigned)(flush_us/frames),
	       (unsigned)(map_us/frames), (unsigned)(copy_us/frames));

	flush_us = map_us = copy_us = 0; frames = 0; last_us = t3;
}


static EGLBoolean panfrost_swap_buffers(_EGLDisplay *disp, _EGLSurface *draw)
{
	uint64_t const t0 = swap_now_us();
	struct dri2_egl_surface  *dri2_surf = dri2_egl_surface(draw);
	struct Genode_egl_window *window    = dri2_surf->g_win;
	struct dri2_egl_context  *dri2_ctx  = dri2_egl_context(_eglGetCurrentContext());

	/* submits the frame */
	dri2_flush_drawable_for_swapbuffers(disp, draw);
	dri_invalidate_drawable(dri2_surf->dri_drawable);

	if (!dri2_ctx || !dri2_surf->back_image)
		return EGL_TRUE;

	uint64_t const t1 = swap_now_us();

	/*
	 * Mapping for reading waits until the GPU has finished the frame and
	 * invalidates the CPU cache for the image if its mapping is cached
	 */
	void *map_data = NULL;
	int   stride   = 0;
	char *pixels   = dri2_map_image(dri2_ctx->dri_context, dri2_surf->back_image,
	                                0, 0, dri2_surf->base.Width,
	                                dri2_surf->base.Height,
	                                __DRI_IMAGE_TRANSFER_READ, &stride, &map_data);
	if (!pixels)
		return EGL_TRUE;

	uint64_t const t2 = swap_now_us();

	int const dst_stride = window->width*BYTES_PER_PIXEL;
	int const width      = MIN2(window->width,  dri2_surf->base.Width);
	int const height     = MIN2(window->height, dri2_surf->base.Height);

	dri2_genode_copy_rows(pixels, stride, (char *)window->addr, dst_stride,
	                      width*BYTES_PER_PIXEL, height);

	dri2_unmap_image(dri2_ctx->dri_context, dri2_surf->back_image, map_data);

	swap_stats(t0, t1, t2, swap_now_us());

	return EGL_TRUE;
}


static int panfrost_image_get_buffers(struct dri_drawable *drawable,
                                      unsigned int format, uint32_t *stamp,
                                      void *loader_private, uint32_t buffer_mask,
                                      struct __DRIimageList *buffers)
{
	struct dri2_egl_surface *dri2_surf = loader_private;

	(void)drawable; (void)format; (void)stamp; (void)buffer_mask;

	buffers->image_mask = __DRI_IMAGE_BUFFER_BACK;
	buffers->front      = NULL;
	buffers->back       = dri2_surf->back_image;

	return 1;
}


static void panfrost_flush_front_buffer(struct dri_drawable *drawable,
                                        void *loader_private)
{
	(void)drawable; (void)loader_private;
}


static unsigned panfrost_get_capability(void *loader_private, enum dri_loader_cap cap)
{
	(void)loader_private;

	switch (cap) {
	case DRI_LOADER_CAP_RGBA_ORDERING: return 1;
	default:                           return 0;
	}
}


static const __DRIimageLoaderExtension image_loader_extension = {
	.base = { __DRI_IMAGE_LOADER, 2 },

	.getBuffers       = panfrost_image_get_buffers,
	.flushFrontBuffer = panfrost_flush_front_buffer,
	.getCapability    = panfrost_get_capability,
};


static const __DRIextension *image_loader_extensions[] = {
	&image_loader_extension.base,
	&image_lookup_extension.base,
	NULL,
};


static const struct dri2_egl_display_vtbl display_vtbl = {
	.create_window_surface = dri2_genode_create_window_surface,
	.create_pixmap_surface = dri2_genode_create_pixmap_surface,
	.destroy_surface       = dri2_genode_destroy_surface,
	.create_image          = dri2_create_image_khr,
	.swap_interval         = dri2_genode_swap_interval,
	.swap_buffers          = panfrost_swap_buffers,
	.get_dri_drawable      = dri2_surface_get_dri_drawable,
};


EGLBoolean dri2_initialize_genode_backend(_EGLDisplay *disp)
{
	struct dri2_egl_display *dri2_dpy = dri2_egl_display(disp);

	/*
	 * EGL tries a second time with software rendering forced when the first
	 * initialization failed. This back end has the GPU only; software
	 * rendering is the back end 'mesa26_gpu-softpipe'.
	 */
	if (disp->Options.ForceSoftware)
		return _eglError(EGL_NOT_INITIALIZED,
		                 "genode: panfrost back end has no software rendering");

	/* select the panthor back end of libdrm26, opens the Gpu session */
	libdrm26_init_panthor();

	/*
	 * No DRM device node exists that EGL could enumerate, the display is
	 * attached to the software EGL device (affects EGL_EXT_device_query
	 * only).
	 */
	disp->Device = _eglFindDevice(-1, true);

	dri2_dpy->fd_render_gpu  = LIBDRM26_PANTHOR_FD;
	dri2_dpy->fd_display_gpu = LIBDRM26_PANTHOR_FD;
	dri2_dpy->driver_name    = strdup("panfrost");
	if (!dri2_dpy->driver_name)
		return _eglError(EGL_BAD_ALLOC, "genode: panfrost");

	/* a hardware driver: 'swrast' stays false, the screen type is DRI3 */
	dri2_detect_swrast_kopper(disp);

	dri2_dpy->loader_extensions = image_loader_extensions;

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
