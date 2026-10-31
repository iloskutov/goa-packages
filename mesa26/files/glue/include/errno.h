/*
 * \brief  Error codes Mesa uses that the libc (FreeBSD) does not have
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * Same values as the stock Mesa port (repos/libports/src/lib/mesa/include).
 * 'ETIME' is what a timed-out syncobj wait reports; the panthor back end of
 * libdrm26 sets it for the Linux error of that name.
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _MESA26__ERRNO_H_
#define _MESA26__ERRNO_H_

#include <sys/errno.h>

#define ENODATA 61
#define ETIME   62

#endif /* _MESA26__ERRNO_H_ */
