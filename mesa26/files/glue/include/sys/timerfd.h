/*
 * \brief  Stand-in for Linux' timerfd
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * As <sys/eventfd.h> of this directory: used by 'panfrost_kmod.c' only,
 * which is never selected on Genode. Creating a timer descriptor fails.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _MESA26__SYS__TIMERFD_H_
#define _MESA26__SYS__TIMERFD_H_

#include <errno.h>
#include <time.h>

enum { TFD_TIMER_ABSTIME = 1, TFD_CLOEXEC = 02000000, TFD_NONBLOCK = 04000 };

static inline int timerfd_create(int clockid, int flags)
{
	(void)clockid; (void)flags;
	errno = ENOSYS;
	return -1;
}

static inline int timerfd_settime(int fd, int flags,
                                  struct itimerspec const *new_value,
                                  struct itimerspec *old_value)
{
	(void)fd; (void)flags; (void)new_value; (void)old_value;
	errno = EBADF;
	return -1;
}

#endif /* _MESA26__SYS__TIMERFD_H_ */
