/*
 * \brief  DRM ioctl dispatcher of libdrm26 (Mesa 26, panthor back end)
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * Counterpart of repos/libports/src/lib/libdrm/ioctl_dispatch.cc for the
 * second Mesa: libdrm's 'ioctl' calls arrive here as 'genode_ioctl', Mesa's
 * 'os_mmap', 'os_munmap' and 'sync_wait' as 'drm_mmap', 'drm_munmap' and
 * 'drm_poll' (patch genode_drm.patch), and the
 * 'close' calls of the panfrost driver as 'genode_drm_close' (renamed by the
 * build, mk/panfrost26-common.inc).
 *
 * The file descriptor of the GPU device is not a libc descriptor but the
 * constant 'LIBDRM26_PANTHOR_FD' (libdrm26.h). Everything that does not
 * refer to a descriptor of the back end is passed on to the libc.
 */

/*
 * SPDX-License-Identifier: MIT
 */

/* Genode includes */
#include <base/log.h>

/* local includes */
#include "libdrm26.h"

extern "C" {
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>
}


static bool _panthor_initialized;


void libdrm26_init_panthor()
{
	if (_panthor_initialized)
		return;

	panthor_drm_init();
	_panthor_initialized = true;
}


/**
 * Perform I/O control request
 *
 * Called by libdrm (drmIoctl) for the device descriptor and by Mesa
 * (panthor_kmod.c) for the descriptors the back end handed out as dma-buf
 * and sync-file descriptors.
 */
extern "C" int genode_ioctl(int fd, unsigned long request, void *arg)
{
	if (_panthor_initialized && panthor_drm_owns_fd(fd))
		return panthor_drm_ioctl(fd, request, arg);

	Genode::error("libdrm26: ioctl ", Genode::Hex(request),
	              " on descriptor ", fd, " that is not a GPU descriptor");
	errno = ENOTTY;
	return -1;
}


/**
 * Map a buffer object (offset obtained by DRM_IOCTL_PANTHOR_BO_MMAP_OFFSET)
 * or the page with the flush id (DRM_PANTHOR_USER_FLUSH_ID_MMIO_OFFSET)
 */
extern "C" void *drm_mmap(void *addr, size_t length, int prot, int flags,
                          int fd, off_t offset)
{
	if (_panthor_initialized && panthor_drm_owns_fd(fd))
		return panthor_drm_mmap(fd, offset, length, prot);

	return mmap(addr, length, prot, flags, fd, offset);
}


extern "C" int drm_munmap(void *addr, size_t length)
{
	if (_panthor_initialized && panthor_drm_munmap(addr, length))
		return 0;

	return munmap(addr, length);
}


/**
 * Wait for a sync file (Mesa's 'sync_wait')
 */
extern "C" int drm_poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	if (_panthor_initialized && nfds == 1 && panthor_drm_owns_fd(fds[0].fd))
		return panthor_drm_poll(fds, timeout);

	return poll(fds, nfds, timeout);
}


/**
 * Close a descriptor ('close' of the panfrost driver)
 */
extern "C" int genode_drm_close(int fd)
{
	if (_panthor_initialized && panthor_drm_owns_fd(fd))
		return panthor_drm_close(fd);

	return close(fd);
}
