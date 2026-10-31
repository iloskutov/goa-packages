/*
 * \brief  Interface between Mesa 26's Genode glue, the dispatcher and the
 *         panthor back end of libdrm26
 * \author Ivan Loskutov
 * \date   2026-10-08
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _LIBDRM26_H_
#define _LIBDRM26_H_

/*
 * Descriptor of the GPU device as Mesa sees it. It is not a libc descriptor
 * (the stock Mesa port uses 10042..10044 for etnaviv, iris and lima the same
 * way). Descriptors the back end hands out for sync files and dma-bufs lie
 * in the range LIBDRM26_FD_FIRST..LIBDRM26_FD_LAST. Mesa's
 * 'os_dupfd_cloexec' passes all of them through unchanged (patch
 * genode_drm.patch).
 */
enum {
	LIBDRM26_PANTHOR_FD = 10045,
	LIBDRM26_FD_FIRST   = 10100,
	LIBDRM26_FD_LAST    = 19999
};

#ifdef __cplusplus

#include <sys/types.h>

struct pollfd;

/*
 * Back end (ioctl_panthor.cc), called by the dispatcher (ioctl_dispatch.cc)
 */

/* open the Gpu session, called once */
void  panthor_drm_init();

/* true for the device descriptor and for descriptors the back end handed out */
bool  panthor_drm_owns_fd(int fd);

/* returns 0 or -1 with 'errno' set (libc values) */
int   panthor_drm_ioctl(int fd, unsigned long request, void *arg);

/* returns MAP_FAILED on error */
void *panthor_drm_mmap(int fd, off_t offset, size_t length, int prot);

/* returns false if 'addr' is not a mapping of the back end */
bool  panthor_drm_munmap(void *addr, size_t length);

/* 'poll' on one sync-file descriptor, result as poll */
int   panthor_drm_poll(struct pollfd *fds, int timeout_ms);

/* release a sync-file or dma-buf descriptor, no effect for the device */
int   panthor_drm_close(int fd);

extern "C" {
#endif

/**
 * Select and initialize the panthor back end
 *
 * Called by the EGL back end 'mesa26_gpu-panfrost' before the screen is
 * created.
 */
void libdrm26_init_panthor(void);

#ifdef __cplusplus
}
#endif

#endif /* _LIBDRM26_H_ */
