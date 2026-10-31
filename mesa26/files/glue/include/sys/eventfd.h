/*
 * \brief  Stand-in for Linux' eventfd
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * Only 'panfrost_kmod.c' uses it, the back end for the kernel driver of the
 * job-manager Malis ("panfrost"), which is compiled but never selected on
 * Genode: the device reports the driver name "panthor". Creating an eventfd
 * fails.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _MESA26__SYS__EVENTFD_H_
#define _MESA26__SYS__EVENTFD_H_

#include <errno.h>
#include <stdint.h>

typedef uint64_t eventfd_t;

static inline int eventfd(unsigned int initval, int flags)
{
	(void)initval; (void)flags;
	errno = ENOSYS;
	return -1;
}

static inline int eventfd_read(int fd, eventfd_t *value)
{
	(void)fd; (void)value;
	errno = EBADF;
	return -1;
}

static inline int eventfd_write(int fd, eventfd_t value)
{
	(void)fd; (void)value;
	errno = EBADF;
	return -1;
}

#endif /* _MESA26__SYS__EVENTFD_H_ */
