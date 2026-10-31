/*
 * \brief  Definitions missing in Genode's libc, included into every Mesa file
 * \author Ivan Loskutov
 * \date   2026-10-08
 */

/*
 * SPDX-License-Identifier: MIT
 */

#ifndef _MESA26__COMPAT_H_
#define _MESA26__COMPAT_H_

/* FreeBSD has no raw monotonic clock (panfrost's performance counters) */
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC

#endif /* _MESA26__COMPAT_H_ */
