/*
 * \brief  DRM back end for the panthor GPU driver (Gpu session)
 * \author Ivan Loskutov
 * \date   2026-10-08
 *
 * Turns the DRM ioctls of Mesa 26's panfrost driver (CSF path, pan_kmod /
 * panthor_kmod) into requests to the panthor GPU driver, as
 * repos/libports/src/lib/libdrm/ioctl_lima.cc does for lima.
 *
 * The protocol is 'include/gpu/panthor_session.h' (Gpu::Panthor, version
 * 1.0): every ioctl but the mapping of
 * a buffer object is a message in an exchange buffer, handed to the driver
 * with 'Gpu::Session::execute'. The payload of a message is the ioctl's
 * argument structure, pointer members hold offsets into the message.
 *
 * The back end opens its own Gpu session. Other than the stock back ends it
 * does not take the session of '/dev/gpu': 'vfs_gpu_connection' of the stock
 * vfs_gpu plugin returns a null pointer in this tree (its file-system
 * pointer is never set, repos/gems/src/lib/vfs/gpu/vfs.cc), so that the
 * blocking 'read' on that file cannot be used as the wait for a completion
 * signal either. A request the driver could not finish at once is therefore
 * POLLED ('_await'). The Genode environment is the application's
 * 'genode_env' if it exports one (as the eglut binding does), else the one
 * of the vfs_gpu plugin, which needs <gpu/> in the VFS.
 *
 * What Mesa issues and what happens here
 * ======================================
 *
 * (found by reading src/panfrost/lib/kmod/{pan_kmod.c,pan_kmod.h,
 * panthor_kmod.c}, src/gallium/drivers/panfrost/{pan_csf.c,pan_fence.c,
 * pan_context.c}, src/gallium/auxiliary/util/u_screen.c, src/loader/loader.c
 * of Mesa 26.2.4 and libdrm's xf86drm.c; "blocks": the caller may sleep)
 *
 * A. DRM core, on the device descriptor
 *
 *  VERSION        drm_version, in/out; pointers name, date, desc with
 *                 lengths in/out. drmGetVersion calls it twice. Callers:
 *                 loader (gallium driver by name), pan_kmod_dev_create
 *                 (kernel back end by name, features by version).
 *                 -> answered from 'Info': "panthor", 1.5.0 on the phone.
 *  GET_CAP        drm_get_cap, in/out. u_screen.c asks DRM_CAP_PRIME.
 *                 -> SYNCOBJ / SYNCOBJ_TIMELINE from 'Info', PRIME 0 (no
 *                 dma-buf import / export), others EINVAL.
 *  GEM_CLOSE      drm_gem_close, in. panthor_kmod_bo_free.
 *                 -> mappings of the object are detached, Op::GEM_CLOSE.
 *  SYNCOBJ_CREATE, _DESTROY, _TRANSFER
 *                 no pointers. One syncobj per context (+ in-fence), per
 *                 fence, per shared object; a timeline syncobj per VM.
 *                 TRANSFER follows EVERY submit (csf_submit_gsubmit).
 *                 -> same-named Op.
 *  SYNCOBJ_WAIT, SYNCOBJ_TIMELINE_WAIT
 *                 in/out (first_signaled); pointers handles (u32 array),
 *                 points (u64 array). BLOCK up to an ABSOLUTE
 *                 CLOCK_MONOTONIC time. Callers: fence_finish (glFinish,
 *                 sync objects), context destruction, and
 *                 panthor_kmod_bo_wait = every CPU mapping of a buffer the
 *                 GPU may use, including the copy at eglSwapBuffers.
 *                 -> same-named Op with a RELATIVE time-out, issued in
 *                 slices of WAIT_SLICE_NS so that other threads get their
 *                 requests through (one message in flight per session).
 *  SYNCOBJ_RESET, _SIGNAL, _QUERY, _TIMELINE_SIGNAL
 *                 pointers handles / points. RESET: panthor_kmod_bo_export;
 *                 the others are not issued by the panfrost driver.
 *                 -> same-named Op.
 *  SYNCOBJ_HANDLE_TO_FD, _FD_TO_HANDLE
 *                 drm_syncobj_handle, in/out. Sync-file export / import for
 *                 pipe fences (pan_fence.c, ctx->in_sync_fd).
 *                 -> same-named Op. The driver's descriptor (an index of
 *                 its per-session table) is handed to Mesa as
 *                 LIBDRM26_FD_FIRST + index and released by Op::FD_CLOSE
 *                 when Mesa closes it ('genode_drm_close').
 *  PRIME_HANDLE_TO_FD, PRIME_FD_TO_HANDLE
 *                 resource export / import and the implicit-sync paths of
 *                 shared objects. -> ENOSYS, the protocol has no dma-bufs.
 *                 Not reached for window surfaces of the EGL platform
 *                 "genode" (private back image).
 *
 * B. panthor, on the device descriptor
 *
 *  DEV_QUERY      in/out; pointer 'pointer' (result, 'size' bytes). GPU,
 *                 CSIF, TIMESTAMP (again per timestamp query), GROUP
 *                 PRIORITIES at device creation. -> Op::DEV_QUERY.
 *  VM_CREATE, VM_DESTROY, VM_GET_STATE
 *                 no pointers. One VM per screen. -> same-named Op.
 *  VM_BIND        in; pointers ops.array and each op's syncs.array. Map:
 *                 synchronous, one op, at object creation (may block on
 *                 the VM); unmap: asynchronous with a signal on the VM's
 *                 timeline syncobj. -> Op::VM_BIND.
 *  BO_CREATE      in/out (size, handle). -> Op::BO_CREATE.
 *  BO_MMAP_OFFSET in/out (offset). -> no request: offset = handle << 12,
 *                 see D.
 *  GROUP_CREATE   in/out (group_handle); pointer queues.array.
 *  GROUP_DESTROY, GROUP_GET_STATE, TILER_HEAP_CREATE, TILER_HEAP_DESTROY
 *                 no pointers. -> same-named Op.
 *  GROUP_SUBMIT   in; pointers queue_submits.array and each one's
 *                 syncs.array. Once per flushed batch; does not wait for
 *                 the job. -> Op::GROUP_SUBMIT with FLAG_SAMPLE_FLUSH_ID.
 *  BO_SET_LABEL   in; pointer label. For every object (driver >= 1.4).
 *                 -> Op::BO_SET_LABEL.
 *  SET_USER_MMIO_OFFSET
 *                 in. Once at device creation (driver >= 1.5), must
 *                 succeed. -> returns 0, see D.
 *  BO_SYNC        only for a driver >= 1.7. -> Op::BO_SYNC.
 *  BO_QUERY_INFO  only for a driver >= 1.7, for imported objects.
 *                 -> Op::BO_QUERY_INFO (never reached: no import without
 *                 PRIME).
 *
 *  Cached buffer objects: if the session provides them ('Info::bo_cached',
 *  protocol >= 1.1), the back end reports the DRM driver version 1.7
 *  instead of the kernel driver's 1.5, because Mesa's kmod layer derives
 *  the buffer-object flag WB_MMAP from that version; and it reports
 *  'selected_coherency = NONE' in the GPU information, a member the 1.5
 *  kernel leaves zero, which would read as "ACE-Lite, no cache
 *  maintenance needed". BO_CREATE carries the flag to the driver as it
 *  is.
 *
 * C. Other descriptors
 *
 *  DMA_BUF_IOCTL_{EXPORT,IMPORT}_SYNC_FILE on a dma-buf: only behind PRIME.
 *                 -> ENOSYS.
 *  poll (sync_wait) on a sync file: only behind PRIME in the driver; the
 *                 protocol has no wait on a sync-file descriptor.
 *                 -> implemented as import into a temporary syncobj + wait.
 *  SYNC_IOC_MERGE (sync_accumulate): Mesa calls the libc's ioctl, which
 *                 fails for these descriptors. Reached by server-side waits
 *                 on native fences only. Not routed here.
 *  close          -> Op::FD_CLOSE for sync files, nothing for the device.
 *
 * D. Mappings
 *
 *  offset of BO_MMAP_OFFSET: 'Gpu::Session::map_cpu(handle)', the dataspace
 *                 is attached and stays attached until the object is closed
 *                 (munmap only drops a use count). The memory is uncached
 *                 unless the object was created with WB_MMAP.
 *  DRM_PANTHOR_USER_FLUSH_ID_MMIO_OFFSET, one page: the LATEST_FLUSH_ID
 *                 register, mandatory at device creation. The driver cannot
 *                 hand the page out. -> a private zeroed page: Mesa reads 0
 *                 into 'latest_flush' (= flush unconditionally) and the
 *                 driver replaces it at submit time (FLAG_SAMPLE_FLUSH_ID).
 *
 * E. Errors
 *
 *  The driver reports Linux errno values, Mesa compares with the libc's:
 *  translated in '_libc_errno'. ETIME (time-out of a wait) is 62 for Mesa
 *  (src/lib/mesa26/include/errno.h). EINTR and EAGAIN make drmIoctl repeat
 *  the request, as on Linux.
 */

/*
 * SPDX-License-Identifier: MIT
 */

/* Genode includes */
#include <base/attached_dataspace.h>
#include <base/heap.h>
#include <base/log.h>
#include <base/registry.h>
#include <gpu_session/connection.h>
#include <gpu/panthor_session.h>
#include <util/reconstructible.h>

#include <vfs_gpu.h>

extern "C" {
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ioccom.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <drm.h>
#include <drm-uapi/panthor_drm.h>
#include <drm-uapi/dma-buf.h>
#include <libdrm_macros.h>
}

/* local includes */
#include "libdrm26.h"

/* value Mesa is compiled with (src/lib/mesa26/include/errno.h) */
#ifndef ETIME
#define ETIME 62
#endif

namespace Panthor_drm {

	using Genode::Env;
	using Genode::Heap;
	using Genode::Constructible;
	using Genode::Attached_dataspace;
	using Genode::Registry;
	using Genode::Dataspace_capability;
	using Genode::Hex;
	using Genode::error;
	using Genode::log;
	using Genode::min;
	using Genode::destroy;
	using namespace Gpu::Panthor;

	enum { EXCHANGE_BUFFER_SIZE = 64u*1024 };

	/* one slice of a blocking wait, see 'Device::_wait' */
	enum : int64_t { WAIT_SLICE_NS = 100ll*1000*1000 };

	/* limits of what is copied from the caller into one message */
	enum : uint32_t { MAX_ELEMENTS = 1024, MAX_QUERY_SIZE = 4096 };

	/* one log line per request, set by the environment variable LIBDRM26_VERBOSE */
	static bool verbose = false;

	/*
	 * Counters of what a client asks for, one log line every 5 seconds
	 * (environment variable LIBDRM26_STATS): how many requests go to the
	 * driver and, through attach / detach of buffer objects, to core.
	 */
	struct Stats
	{
		bool     enabled = false;
		uint64_t start_ms = 0;
		unsigned ioctls = 0, executes = 0, maps = 0, unmaps = 0, reused = 0, creates = 0,
		         closes = 0, submits = 0, binds = 0, waits = 0;

		static uint64_t now_ms()
		{
			struct timespec ts { };
			clock_gettime(CLOCK_MONOTONIC, &ts);
			return (uint64_t)ts.tv_sec*1000 + (uint64_t)ts.tv_nsec/1000000;
		}

		void tick()
		{
			if (!enabled)
				return;

			uint64_t const now = now_ms();
			if (!start_ms) start_ms = now;
			if (now - start_ms < 5000)
				return;

			Genode::log("libdrm26 stats, ", now - start_ms, " ms: ", ioctls,
			            " ioctls, ", executes, " messages to the driver, ",
			            submits, " submits, ", waits, " waits, ", binds,
			            " vm binds, ", creates, " bo creates, ", closes,
			            " closes, ", maps, " maps (map_cpu + attach), ",
			            reused, " mappings reused, ", unmaps, " detaches");

			bool const e = enabled;
			*this = { };
			enabled = e; start_ms = now;
		}
	};

	static Stats stats { };

	struct Device;

	static constexpr unsigned long command_number(unsigned long request)
	{
		return request & 0xffu;
	}

	static char const *command_name(unsigned long request);
	static int  _libc_errno(int32_t linux_errno);
	static int  _error(int err) { errno = err; return -1; }
}


static char const *Panthor_drm::command_name(unsigned long request)
{
	if (IOCGROUP(request) == DMA_BUF_BASE) {
		switch (command_number(request)) {
		case command_number(DMA_BUF_IOCTL_SYNC):             return "DMA_BUF_IOCTL_SYNC";
		case command_number(DMA_BUF_IOCTL_EXPORT_SYNC_FILE): return "DMA_BUF_IOCTL_EXPORT_SYNC_FILE";
		case command_number(DMA_BUF_IOCTL_IMPORT_SYNC_FILE): return "DMA_BUF_IOCTL_IMPORT_SYNC_FILE";
		default:                                             return "<unknown dma-buf>";
		}
	}

	if (IOCGROUP(request) != DRM_IOCTL_BASE)
		return "<non-DRM>";

	unsigned long const cmd = command_number(request);

	if (cmd >= DRM_COMMAND_BASE && cmd < DRM_COMMAND_END) {
		switch (cmd - DRM_COMMAND_BASE) {
		case DRM_PANTHOR_DEV_QUERY:            return "DRM_PANTHOR_DEV_QUERY";
		case DRM_PANTHOR_VM_CREATE:            return "DRM_PANTHOR_VM_CREATE";
		case DRM_PANTHOR_VM_DESTROY:           return "DRM_PANTHOR_VM_DESTROY";
		case DRM_PANTHOR_VM_BIND:              return "DRM_PANTHOR_VM_BIND";
		case DRM_PANTHOR_VM_GET_STATE:         return "DRM_PANTHOR_VM_GET_STATE";
		case DRM_PANTHOR_BO_CREATE:            return "DRM_PANTHOR_BO_CREATE";
		case DRM_PANTHOR_BO_MMAP_OFFSET:       return "DRM_PANTHOR_BO_MMAP_OFFSET";
		case DRM_PANTHOR_GROUP_CREATE:         return "DRM_PANTHOR_GROUP_CREATE";
		case DRM_PANTHOR_GROUP_DESTROY:        return "DRM_PANTHOR_GROUP_DESTROY";
		case DRM_PANTHOR_GROUP_SUBMIT:         return "DRM_PANTHOR_GROUP_SUBMIT";
		case DRM_PANTHOR_GROUP_GET_STATE:      return "DRM_PANTHOR_GROUP_GET_STATE";
		case DRM_PANTHOR_TILER_HEAP_CREATE:    return "DRM_PANTHOR_TILER_HEAP_CREATE";
		case DRM_PANTHOR_TILER_HEAP_DESTROY:   return "DRM_PANTHOR_TILER_HEAP_DESTROY";
		case DRM_PANTHOR_BO_SET_LABEL:         return "DRM_PANTHOR_BO_SET_LABEL";
		case DRM_PANTHOR_SET_USER_MMIO_OFFSET: return "DRM_PANTHOR_SET_USER_MMIO_OFFSET";
		case DRM_PANTHOR_BO_SYNC:              return "DRM_PANTHOR_BO_SYNC";
		case DRM_PANTHOR_BO_QUERY_INFO:        return "DRM_PANTHOR_BO_QUERY_INFO";
		default:                               return "<unknown panthor>";
		}
	}

	switch (cmd) {
	case command_number(DRM_IOCTL_VERSION):                 return "DRM_IOCTL_VERSION";
	case command_number(DRM_IOCTL_GET_CAP):                 return "DRM_IOCTL_GET_CAP";
	case command_number(DRM_IOCTL_GEM_CLOSE):               return "DRM_IOCTL_GEM_CLOSE";
	case command_number(DRM_IOCTL_PRIME_HANDLE_TO_FD):      return "DRM_IOCTL_PRIME_HANDLE_TO_FD";
	case command_number(DRM_IOCTL_PRIME_FD_TO_HANDLE):      return "DRM_IOCTL_PRIME_FD_TO_HANDLE";
	case command_number(DRM_IOCTL_SYNCOBJ_CREATE):          return "DRM_IOCTL_SYNCOBJ_CREATE";
	case command_number(DRM_IOCTL_SYNCOBJ_DESTROY):         return "DRM_IOCTL_SYNCOBJ_DESTROY";
	case command_number(DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD):    return "DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD";
	case command_number(DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE):    return "DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE";
	case command_number(DRM_IOCTL_SYNCOBJ_TRANSFER):        return "DRM_IOCTL_SYNCOBJ_TRANSFER";
	case command_number(DRM_IOCTL_SYNCOBJ_WAIT):            return "DRM_IOCTL_SYNCOBJ_WAIT";
	case command_number(DRM_IOCTL_SYNCOBJ_RESET):           return "DRM_IOCTL_SYNCOBJ_RESET";
	case command_number(DRM_IOCTL_SYNCOBJ_SIGNAL):          return "DRM_IOCTL_SYNCOBJ_SIGNAL";
	case command_number(DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT):   return "DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT";
	case command_number(DRM_IOCTL_SYNCOBJ_QUERY):           return "DRM_IOCTL_SYNCOBJ_QUERY";
	case command_number(DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL): return "DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL";
	default:                                                return "<unknown DRM>";
	}
}


/**
 * Translate the (positive) Linux errno of a response into the libc's
 */
static int Panthor_drm::_libc_errno(int32_t e)
{
	switch (e) {
	case LX_ENOENT:      return ENOENT;
	case LX_EINTR:       return EINTR;
	case LX_EBADF:       return EBADF;
	case LX_EAGAIN:      return EAGAIN;
	case LX_ENOMEM:      return ENOMEM;
	case LX_EFAULT:      return EFAULT;
	case LX_EBUSY:       return EBUSY;
	case LX_ENODEV:      return ENODEV;
	case LX_EINVAL:      return EINVAL;
	case LX_EMFILE:      return EMFILE;
	case LX_ENOSPC:      return ENOSPC;
	case LX_ENOSYS:      return ENOSYS;
	case LX_ETIME:       return ETIME;
	case LX_EPROTO:      return EPROTO;
	case LX_EOPNOTSUPP:  return EOPNOTSUPP;
	case LX_ECANCELED:   return ECANCELED;
	case LX_ERESTARTSYS: return EINTR;
	case 1:              return EPERM;
	case 7:              return E2BIG;
	case 13:             return EACCES;
	case 17:             return EEXIST;
	case 34:             return ERANGE;
	case 75:             return EOVERFLOW;
	case 110:            return ETIMEDOUT;
	default:
		Genode::warning("libdrm26: Linux errno ", e, " reported as EINVAL");
		return EINVAL;
	}
}


struct Panthor_drm::Device
{
	/*
	 * The uAPI structures of Mesa's header and the mirrors of the protocol
	 * must agree where a structure is copied as a whole.
	 */
	template <typename UAPI, typename PROTO>
	static void _assert_same()
	{
		static_assert(sizeof(UAPI) == sizeof(PROTO));
	}

	struct Mutex_guard
	{
		pthread_mutex_t &_mutex;
		Mutex_guard(pthread_mutex_t &mutex) : _mutex(mutex) { pthread_mutex_lock(&_mutex); }
		~Mutex_guard() { pthread_mutex_unlock(&_mutex); }
	};

	/* one message in flight per session, one exchange buffer */
	pthread_mutex_t _mutex = PTHREAD_MUTEX_INITIALIZER;

	Env  &_env;
	Heap  _heap { _env.ram(), _env.rm() };

	Constructible<Gpu::Connection> _gpu { };

	Constructible<Attached_dataspace> _info_ds  { };
	Constructible<Attached_dataspace> _exchange { };

	Gpu::Vram_id const _exchange_id { .value = 1 };

	bool _ok = false;

	/* stand-in for the page with the LATEST_FLUSH_ID register */
	void *_flush_id_page = nullptr;

	/*
	 * Mappings of buffer objects
	 */
	struct Mapping : Registry<Mapping>::Element
	{
		uint32_t const     handle;
		Attached_dataspace ds;

		/*
		 * A buffer object stays attached until it is closed: Mesa maps
		 * and unmaps the same objects in every frame (gears: 4 pairs per
		 * frame), and each pair was a 'map_cpu' request to the driver
		 * plus an attach and a detach at core. 'users' counts the
		 * mappings Mesa holds.
		 */
		unsigned users = 1;

		Mapping(Registry<Mapping> &registry, Genode::Local::Constrained_region_map &rm,
		        uint32_t handle,
		        Dataspace_capability cap)
		:
			Registry<Mapping>::Element(registry, *this), handle(handle), ds(rm, cap)
		{ }
	};

	Registry<Mapping> _mappings { };

	Info const &_info() const { return *_info_ds->local_addr<Info>(); }

	/*
	 * The session has the buffer-object interface of panthor 1.7: cached
	 * objects on request (WB_MMAP), BO_SYNC, BO_QUERY_INFO
	 */
	bool _bo_cached() const
	{
		return _info().bo_cached && (_info().version & 0xffff) >= 1;
	}

	/* DRM driver version in which Linux' panthor got that interface */
	enum { WB_MMAP_VERSION_MINOR = 7 };

	unsigned _drm_version_minor() const
	{
		Info const &info = _info();

		if (_bo_cached() && info.drm_version_major == 1
		 && info.drm_version_minor < WB_MMAP_VERSION_MINOR)
			return WB_MMAP_VERSION_MINOR;

		return info.drm_version_minor;
	}

	Device(Device const &) = delete;
	Device &operator = (Device const &) = delete;

	Device(Env &env) : _env(env)
	{
		verbose = getenv("LIBDRM26_VERBOSE") != nullptr;
		stats.enabled = getenv("LIBDRM26_STATS") != nullptr;

		try {
			_gpu.construct(_env);
		} catch (...) {
			error("libdrm26: could not open a Gpu session");
			return;
		}

		_info_ds.construct(_env.rm(), _gpu->info_dataspace());

		Info const &info = _info();

		if (info.magic != MAGIC || (info.version >> 16) != VERSION_MAJOR) {
			error("libdrm26: the Gpu session is not a panthor session of protocol ",
			      (unsigned)VERSION_MAJOR, ".x (magic ", Hex(info.magic),
			      ", version ", Hex(info.version), ")");
			return;
		}

		if (!info.device && !info.drm_core) {
			error("libdrm26: the panthor driver has no GPU");
			return;
		}

		/* protocol test of the driver: panthor's own requests fail with ENODEV */
		if (!info.device)
			Genode::warning("libdrm26: the panthor driver runs without GPU, "
			                "only the requests of the DRM core work");

		Dataspace_capability const ds =
			_gpu->alloc_vram(_exchange_id, EXCHANGE_BUFFER_SIZE);

		if (!ds.valid()) {
			error("libdrm26: no exchange buffer (alloc_vram)");
			return;
		}

		_exchange.construct(_env.rm(), ds);

		log("libdrm26: panthor ", info.drm_version_major, ".",
		    info.drm_version_minor, ".", info.drm_version_patch,
		    ", gpu_id ", Hex(info.gpu.gpu_id),
		    ", core_features ", Hex(info.gpu.core_features),
		    ", buffer objects ",
		    _bo_cached() ? "cached on request, reported as panthor 1.7"
		                 : "uncached");

		_ok = true;
	}

	bool ok() const { return _ok; }

	Message _message(Op op, uint32_t flags = 0)
	{
		return Message(_exchange->local_addr<void>(), EXCHANGE_BUFFER_SIZE, op, flags);
	}

	Header volatile &_header()
	{
		return *_exchange->local_addr<Header volatile>();
	}

	/**
	 * Execute the message in the exchange buffer, wait for its response
	 *
	 * Must be called with the mutex held.
	 *
	 * \return 0, or the positive Linux errno of the response
	 */
	int32_t _execute(Message &msg)
	{
		if (!msg.finish()) {
			error("libdrm26: request does not fit into a message");
			return LX_ENOMEM;
		}

		Gpu::Sequence_number seqno { };

		stats.executes++;

		try {
			seqno = _gpu->execute(_exchange_id, 0);
		} catch (Gpu::Session::Invalid_state) {
			error("libdrm26: 'execute' refused the message");
			return LX_EPROTO;
		}

		_await();

		(void)_gpu->complete(seqno);

		int32_t const result = _header().result;
		return result < 0 ? -result : 0;
	}

	/**
	 * Wait until the driver has finished the message in flight
	 *
	 * Most requests are done when 'execute' returns. A blocked one (a wait,
	 * a contended lock) is polled: a few checks without delay for requests
	 * that finish within microseconds, then with sleeps of POLL_US. The
	 * completion signal of the session is not used, see the file header.
	 */
	void _await()
	{
		enum { SPINS = 200, POLL_US = 500 };

		for (unsigned i = 0; _header().state != Header::DONE; i++)
			if (i >= SPINS)
				usleep(POLL_US);
	}

	int _result(int32_t linux_errno)
	{
		return linux_errno ? _error(_libc_errno(linux_errno)) : 0;
	}

	/**
	 * Request whose argument structure has no pointer members
	 */
	template <typename UAPI, typename PROTO>
	int _simple(Op op, UAPI &arg)
	{
		_assert_same<UAPI, PROTO>();

		Mutex_guard guard { _mutex };

		Message msg = _message(op);
		PROTO &payload = msg.payload<PROTO>();
		memcpy(&payload, &arg, sizeof(arg));

		int32_t const err = _execute(msg);
		if (!err)
			memcpy(&arg, &payload, sizeof(arg));

		return _result(err);
	}

	/**
	 * Copy an object array of the caller behind the payload
	 *
	 * \return pointer to the elements in the message, nullptr on error
	 *         ('count' of 0 is no error, see 'ok')
	 */
	template <typename PROTO>
	PROTO *_copy_array(Message &msg, drm_panthor_obj_array const &src,
	                   Obj_array &dst, bool &ok)
	{
		dst = Obj_array { .stride = sizeof(PROTO), .count = src.count, .array = 0 };

		if (!src.count)
			return nullptr;

		if (src.count > MAX_ELEMENTS || !src.array || !src.stride) {
			ok = false;
			return nullptr;
		}

		PROTO * const elements = msg.append<PROTO>(src.count);
		if (!elements) {
			ok = false;
			return nullptr;
		}

		/* as the kernel: a larger stride is cut, a smaller one zero-filled */
		size_t const n = min((size_t)src.stride, sizeof(PROTO));
		for (uint32_t i = 0; i < src.count; i++)
			memcpy(&elements[i], (char const *)(uintptr_t)src.array + (size_t)i*src.stride, n);

		dst.array = msg.offset(elements);
		return elements;
	}

	template <typename PROTO>
	bool _copy_syncs(Message &msg, PROTO &element)
	{
		/* 'element.syncs' still holds the caller's array description */
		drm_panthor_obj_array src;
		memcpy(&src, &element.syncs, sizeof(src));

		bool ok = true;
		_copy_array<Sync_op>(msg, src, element.syncs, ok);
		return ok;
	}

	/*
	 * DRM core
	 */

	int _version(drm_version &arg)
	{
		static char const name[] = "panthor";
		static char const date[] = "20230801";
		static char const desc[] = "Panthor DRM driver";

		auto copy = [] (char *dst, auto &len, char const *src) {
			size_t const src_len = strlen(src);
			if (dst && len)
				memcpy(dst, src, min((size_t)len, src_len));
			len = src_len;
		};

		Info const &info = _info();

		arg.version_major      = (int)info.drm_version_major;
		arg.version_minor      = (int)_drm_version_minor();
		arg.version_patchlevel = (int)info.drm_version_patch;

		copy(arg.name, arg.name_len, name);
		copy(arg.date, arg.date_len, date);
		copy(arg.desc, arg.desc_len, desc);

		return 0;
	}

	int _get_cap(drm_get_cap &arg)
	{
		switch (arg.capability) {
		case DRM_CAP_SYNCOBJ:          arg.value = _info().cap_syncobj;          return 0;
		case DRM_CAP_SYNCOBJ_TIMELINE: arg.value = _info().cap_syncobj_timeline; return 0;
		case DRM_CAP_PRIME:            arg.value = 0;                            return 0;
		default:                                                                 break;
		}
		return _error(EINVAL);
	}

	void _detach(uint32_t handle)
	{
		_mappings.for_each([&] (Mapping &mapping) {
			if (mapping.handle == handle) {
				stats.unmaps++;
				destroy(_heap, &mapping);
			}
		});
	}

	int _gem_close(drm_gem_close &arg)
	{
		{
			Mutex_guard guard { _mutex };
			_detach(arg.handle);
		}
		return _simple<drm_gem_close, Gem_close>(Op::GEM_CLOSE, arg);
	}

	/* descriptor of the driver's table <-> descriptor Mesa sees */
	static int  _to_mesa_fd(int32_t fd) { return LIBDRM26_FD_FIRST + fd; }
	static bool _table_fd(int fd)       { return fd >= LIBDRM26_FD_FIRST && fd <= LIBDRM26_FD_LAST; }
	static int32_t _to_table_fd(int fd) { return fd - LIBDRM26_FD_FIRST; }

	/*
	 * libdrm's 'drm_syncobj_handle' (2.4.120) has no 'point' member yet, the
	 * structure is copied member by member. 'table_fd' is the descriptor of
	 * the driver's table (in for FD_TO_HANDLE, out for HANDLE_TO_FD).
	 */
	int _syncobj_handle(Op op, drm_syncobj_handle &arg, int32_t &table_fd)
	{
		Mutex_guard guard { _mutex };

		Message msg = _message(op);
		Syncobj_handle &p = msg.payload<Syncobj_handle>();

		p.handle = arg.handle;
		p.flags  = arg.flags;
		p.fd     = table_fd;

		int32_t const err = _execute(msg);
		if (!err) {
			arg.handle = p.handle;
			table_fd   = p.fd;
		}
		return _result(err);
	}

	int _syncobj_handle_to_fd(drm_syncobj_handle &arg)
	{
		int32_t table_fd = -1;
		int const ret = _syncobj_handle(Op::SYNCOBJ_HANDLE_TO_FD, arg, table_fd);
		if (!ret)
			arg.fd = _to_mesa_fd(table_fd);
		return ret;
	}

	int _syncobj_fd_to_handle(drm_syncobj_handle &arg)
	{
		if (!_table_fd(arg.fd))
			return _error(EBADF);

		int32_t table_fd = _to_table_fd(arg.fd);
		return _syncobj_handle(Op::SYNCOBJ_FD_TO_HANDLE, arg, table_fd);
	}

	/**
	 * Remaining time of an absolute CLOCK_MONOTONIC deadline
	 */
	static int64_t _relative(int64_t abs_ns)
	{
		if (abs_ns == INT64_MAX) return INT64_MAX;
		if (abs_ns <= 0)         return 0;

		struct timespec now { };
		clock_gettime(CLOCK_MONOTONIC, &now);

		int64_t const now_ns = (int64_t)now.tv_sec*1000000000ll + now.tv_nsec;
		return abs_ns > now_ns ? abs_ns - now_ns : 0;
	}

	/**
	 * Wait on syncobjs, with or without timeline points
	 *
	 * The wait is issued in slices: while a message is in flight no other
	 * thread of the process can talk to the driver.
	 */
	int _wait(uint32_t const *handles, uint64_t const *points, uint32_t count,
	          int64_t timeout_abs_ns, uint32_t flags, uint32_t &first_signaled,
	          bool timeline)
	{
		if (count > MAX_ELEMENTS || (count && !handles))
			return _error(EINVAL);

		/* the deadline hint cannot be converted for every slice, drop it */
		flags &= ~(uint32_t)SYNCOBJ_WAIT_FLAGS_WAIT_DEADLINE;

		for (;;) {
			int64_t const remaining = _relative(timeout_abs_ns);
			int64_t const slice     = min(remaining, (int64_t)WAIT_SLICE_NS);

			int32_t err;
			{
				Mutex_guard guard { _mutex };

				Message msg = _message(timeline ? Op::SYNCOBJ_TIMELINE_WAIT
				                                : Op::SYNCOBJ_WAIT);

				uint32_t *first = nullptr;
				uint32_t *msg_handles = nullptr;

				if (timeline) {
					Syncobj_timeline_wait &p = msg.payload<Syncobj_timeline_wait>();
					msg_handles = msg.append<uint32_t>(count, 4);
					uint64_t * const msg_points =
						points ? msg.append<uint64_t>(count, 8) : nullptr;

					if (!msg_handles || (points && !msg_points))
						return _error(ENOMEM);

					if (points)
						memcpy(msg_points, points, sizeof(uint64_t)*count);

					p.handles       = msg.offset(msg_handles);
					p.points        = msg.offset(msg_points);
					p.timeout_nsec  = slice;
					p.count_handles = count;
					p.flags         = flags;
					first           = &p.first_signaled;
				} else {
					Syncobj_wait &p = msg.payload<Syncobj_wait>();
					msg_handles = msg.append<uint32_t>(count, 4);
					if (!msg_handles)
						return _error(ENOMEM);

					p.handles       = msg.offset(msg_handles);
					p.timeout_nsec  = slice;
					p.count_handles = count;
					p.flags         = flags;
					first           = &p.first_signaled;
				}

				memcpy(msg_handles, handles, sizeof(uint32_t)*count);

				err = _execute(msg);
				if (!err)
					first_signaled = *first;
			}

			/* time-out of a slice that was shorter than the caller's */
			if (err == LX_ETIME && slice < remaining)
				continue;

			return _result(err);
		}
	}

	int _syncobj_wait(drm_syncobj_wait &arg)
	{
		return _wait((uint32_t const *)(uintptr_t)arg.handles, nullptr,
		             arg.count_handles, arg.timeout_nsec, arg.flags,
		             arg.first_signaled, false);
	}

	int _syncobj_timeline_wait(drm_syncobj_timeline_wait &arg)
	{
		return _wait((uint32_t const *)(uintptr_t)arg.handles,
		             (uint64_t const *)(uintptr_t)arg.points,
		             arg.count_handles, arg.timeout_nsec, arg.flags,
		             arg.first_signaled, true);
	}

	int _syncobj_array(Op op, drm_syncobj_array &arg)
	{
		if (arg.count_handles > MAX_ELEMENTS || (arg.count_handles && !arg.handles))
			return _error(EINVAL);

		Mutex_guard guard { _mutex };

		Message msg = _message(op);
		Syncobj_array &p = msg.payload<Syncobj_array>();
		uint32_t * const handles = msg.append<uint32_t>(arg.count_handles, 4);
		if (!handles)
			return _error(ENOMEM);

		memcpy(handles, (void const *)(uintptr_t)arg.handles,
		       sizeof(uint32_t)*arg.count_handles);

		p.handles       = msg.offset(handles);
		p.count_handles = arg.count_handles;

		return _result(_execute(msg));
	}

	int _syncobj_timeline_array(Op op, drm_syncobj_timeline_array &arg, bool query)
	{
		uint32_t const count = arg.count_handles;

		if (count > MAX_ELEMENTS || (count && (!arg.handles || !arg.points)))
			return _error(EINVAL);

		Mutex_guard guard { _mutex };

		Message msg = _message(op);
		Syncobj_timeline_array &p = msg.payload<Syncobj_timeline_array>();
		uint32_t * const handles = msg.append<uint32_t>(count, 4);
		uint64_t * const points  = msg.append<uint64_t>(count, 8);
		if (!handles || !points)
			return _error(ENOMEM);

		memcpy(handles, (void const *)(uintptr_t)arg.handles, sizeof(uint32_t)*count);
		memcpy(points,  (void const *)(uintptr_t)arg.points,  sizeof(uint64_t)*count);

		p.handles       = msg.offset(handles);
		p.points        = msg.offset(points);
		p.count_handles = count;
		p.flags         = arg.flags;

		int32_t const err = _execute(msg);
		if (!err && query)
			memcpy((void *)(uintptr_t)arg.points, points, sizeof(uint64_t)*count);

		return _result(err);
	}

	/*
	 * panthor
	 */

	int _dev_query(drm_panthor_dev_query &arg)
	{
		if (arg.size > MAX_QUERY_SIZE)
			return _error(EINVAL);

		Mutex_guard guard { _mutex };

		Message msg = _message(Op::DEV_QUERY);
		Dev_query &p = msg.payload<Dev_query>();

		char *result = nullptr;
		if (arg.pointer) {
			result = msg.append<char>(arg.size, 8);
			if (!result)
				return _error(ENOMEM);
		}

		p.type    = arg.type;
		p.size    = arg.pointer ? arg.size : 0;
		p.pointer = msg.offset(result);

		int32_t const err = _execute(msg);
		if (err)
			return _result(err);

		/* the driver zero-fills what its structure does not have */
		if (result)
			memcpy((void *)(uintptr_t)arg.pointer, result, arg.size);

		/*
		 * 'selected_coherency' exists since panthor 1.7, the kernel
		 * driver leaves it 0 (ACE-Lite). The GPU is not coherent with the
		 * CPUs' caches: a client that believes otherwise would skip the
		 * cache maintenance of its cached buffer objects.
		 */
		if (result && arg.type == DRM_PANTHOR_DEV_QUERY_GPU_INFO && _bo_cached()
		 && arg.size >= offsetof(drm_panthor_gpu_info, selected_coherency)
		              + sizeof(uint32_t))
			((drm_panthor_gpu_info *)(uintptr_t)arg.pointer)->selected_coherency =
				DRM_PANTHOR_GPU_COHERENCY_NONE;

		arg.size = p.size;
		return 0;
	}

	int _vm_bind(drm_panthor_vm_bind &arg)
	{
		Mutex_guard guard { _mutex };

		Message msg = _message(Op::VM_BIND);
		Vm_bind &p = msg.payload<Vm_bind>();

		p.vm_id = arg.vm_id;
		p.flags = arg.flags;

		bool ok = true;
		Vm_bind_op * const ops = _copy_array<Vm_bind_op>(msg, arg.ops, p.ops, ok);

		for (uint32_t i = 0; ok && ops && i < arg.ops.count; i++)
			ok = _copy_syncs(msg, ops[i]);

		if (!ok)
			return _error(EINVAL);

		return _result(_execute(msg));
	}

	int _group_create(drm_panthor_group_create &arg)
	{
		_assert_same<drm_panthor_group_create, Group_create>();

		Mutex_guard guard { _mutex };

		Message msg = _message(Op::GROUP_CREATE);
		Group_create &p = msg.payload<Group_create>();
		memcpy(&p, &arg, sizeof(arg));

		bool ok = true;
		_copy_array<Queue_create>(msg, arg.queues, p.queues, ok);
		if (!ok)
			return _error(EINVAL);

		int32_t const err = _execute(msg);
		if (!err)
			arg.group_handle = p.group_handle;

		return _result(err);
	}

	int _group_submit(drm_panthor_group_submit &arg)
	{
		Mutex_guard guard { _mutex };

		/*
		 * Mesa read 'latest_flush' from the stand-in page (0). The driver
		 * stores the register's current value into every queue submit that
		 * has a command stream.
		 */
		Message msg = _message(Op::GROUP_SUBMIT, Header::FLAG_SAMPLE_FLUSH_ID);
		Group_submit &p = msg.payload<Group_submit>();

		p.group_handle = arg.group_handle;

		bool ok = true;
		Queue_submit * const submits =
			_copy_array<Queue_submit>(msg, arg.queue_submits, p.queue_submits, ok);

		for (uint32_t i = 0; ok && submits && i < arg.queue_submits.count; i++)
			ok = _copy_syncs(msg, submits[i]);

		if (!ok)
			return _error(EINVAL);

		return _result(_execute(msg));
	}

	int _bo_set_label(drm_panthor_bo_set_label &arg)
	{
		Mutex_guard guard { _mutex };

		Message msg = _message(Op::BO_SET_LABEL);
		Bo_set_label &p = msg.payload<Bo_set_label>();

		char *label = nullptr;
		if (arg.label) {
			label = msg.append_string((char const *)(uintptr_t)arg.label);
			if (!label)
				return _error(E2BIG);
		}

		p.handle = arg.handle;
		p.label  = msg.offset(label);

		return _result(_execute(msg));
	}

	int _bo_sync(drm_panthor_bo_sync &arg)
	{
		Mutex_guard guard { _mutex };

		Message msg = _message(Op::BO_SYNC);
		Bo_sync &p = msg.payload<Bo_sync>();

		bool ok = true;
		_copy_array<Bo_sync_op>(msg, arg.ops, p.ops, ok);
		if (!ok)
			return _error(EINVAL);

		return _result(_execute(msg));
	}

	int _bo_mmap_offset(drm_panthor_bo_mmap_offset &arg)
	{
		/* resolved by 'mmap', never collides with the user-MMIO offset */
		arg.offset = (uint64_t)arg.handle << 12;
		return 0;
	}

	int _panthor_ioctl(unsigned nr, void *arg)
	{
		switch (nr) {

		case DRM_PANTHOR_DEV_QUERY:
			return _dev_query(*(drm_panthor_dev_query *)arg);

		case DRM_PANTHOR_VM_CREATE:
			return _simple<drm_panthor_vm_create, Vm_create>(
				Op::VM_CREATE, *(drm_panthor_vm_create *)arg);

		case DRM_PANTHOR_VM_DESTROY:
			return _simple<drm_panthor_vm_destroy, Vm_destroy>(
				Op::VM_DESTROY, *(drm_panthor_vm_destroy *)arg);

		case DRM_PANTHOR_VM_BIND:
			return _vm_bind(*(drm_panthor_vm_bind *)arg);

		case DRM_PANTHOR_VM_GET_STATE:
			return _simple<drm_panthor_vm_get_state, Vm_get_state>(
				Op::VM_GET_STATE, *(drm_panthor_vm_get_state *)arg);

		case DRM_PANTHOR_BO_CREATE:
			return _simple<drm_panthor_bo_create, Bo_create>(
				Op::BO_CREATE, *(drm_panthor_bo_create *)arg);

		case DRM_PANTHOR_BO_MMAP_OFFSET:
			return _bo_mmap_offset(*(drm_panthor_bo_mmap_offset *)arg);

		case DRM_PANTHOR_GROUP_CREATE:
			return _group_create(*(drm_panthor_group_create *)arg);

		case DRM_PANTHOR_GROUP_DESTROY:
			return _simple<drm_panthor_group_destroy, Group_destroy>(
				Op::GROUP_DESTROY, *(drm_panthor_group_destroy *)arg);

		case DRM_PANTHOR_GROUP_SUBMIT:
			return _group_submit(*(drm_panthor_group_submit *)arg);

		case DRM_PANTHOR_GROUP_GET_STATE:
			return _simple<drm_panthor_group_get_state, Group_get_state>(
				Op::GROUP_GET_STATE, *(drm_panthor_group_get_state *)arg);

		case DRM_PANTHOR_TILER_HEAP_CREATE:
			return _simple<drm_panthor_tiler_heap_create, Tiler_heap_create>(
				Op::TILER_HEAP_CREATE, *(drm_panthor_tiler_heap_create *)arg);

		case DRM_PANTHOR_TILER_HEAP_DESTROY:
			return _simple<drm_panthor_tiler_heap_destroy, Tiler_heap_destroy>(
				Op::TILER_HEAP_DESTROY, *(drm_panthor_tiler_heap_destroy *)arg);

		case DRM_PANTHOR_BO_SET_LABEL:
			return _bo_set_label(*(drm_panthor_bo_set_label *)arg);

		case DRM_PANTHOR_SET_USER_MMIO_OFFSET:
			/* the offset is the default one, see 'mmap' */
			return 0;

		case DRM_PANTHOR_BO_SYNC:
			return _bo_sync(*(drm_panthor_bo_sync *)arg);

		case DRM_PANTHOR_BO_QUERY_INFO:
			return _simple<drm_panthor_bo_query_info, Bo_query_info>(
				Op::BO_QUERY_INFO, *(drm_panthor_bo_query_info *)arg);

		default:
			return _error(ENOSYS);
		}
	}

	int _core_ioctl(unsigned long request, void *arg)
	{
		switch (command_number(request)) {

		case command_number(DRM_IOCTL_VERSION):
			return _version(*(drm_version *)arg);

		case command_number(DRM_IOCTL_GET_CAP):
			return _get_cap(*(drm_get_cap *)arg);

		case command_number(DRM_IOCTL_GEM_CLOSE):
			return _gem_close(*(drm_gem_close *)arg);

		case command_number(DRM_IOCTL_SYNCOBJ_CREATE):
			return _simple<drm_syncobj_create, Syncobj_create>(
				Op::SYNCOBJ_CREATE, *(drm_syncobj_create *)arg);

		case command_number(DRM_IOCTL_SYNCOBJ_DESTROY):
			return _simple<drm_syncobj_destroy, Syncobj_destroy>(
				Op::SYNCOBJ_DESTROY, *(drm_syncobj_destroy *)arg);

		case command_number(DRM_IOCTL_SYNCOBJ_TRANSFER):
			return _simple<drm_syncobj_transfer, Syncobj_transfer>(
				Op::SYNCOBJ_TRANSFER, *(drm_syncobj_transfer *)arg);

		case command_number(DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD):
			return _syncobj_handle_to_fd(*(drm_syncobj_handle *)arg);

		case command_number(DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE):
			return _syncobj_fd_to_handle(*(drm_syncobj_handle *)arg);

		case command_number(DRM_IOCTL_SYNCOBJ_WAIT):
			return _syncobj_wait(*(drm_syncobj_wait *)arg);

		case command_number(DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT):
			return _syncobj_timeline_wait(*(drm_syncobj_timeline_wait *)arg);

		case command_number(DRM_IOCTL_SYNCOBJ_RESET):
			return _syncobj_array(Op::SYNCOBJ_RESET, *(drm_syncobj_array *)arg);

		case command_number(DRM_IOCTL_SYNCOBJ_SIGNAL):
			return _syncobj_array(Op::SYNCOBJ_SIGNAL, *(drm_syncobj_array *)arg);

		case command_number(DRM_IOCTL_SYNCOBJ_QUERY):
			return _syncobj_timeline_array(Op::SYNCOBJ_QUERY,
			                               *(drm_syncobj_timeline_array *)arg, true);

		case command_number(DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL):
			return _syncobj_timeline_array(Op::SYNCOBJ_TIMELINE_SIGNAL,
			                               *(drm_syncobj_timeline_array *)arg, false);

		default:
			/* PRIME and everything else */
			return _error(ENOSYS);
		}
	}

	int ioctl(int fd, unsigned long request, void *arg)
	{
		if (verbose)
			log("libdrm26: ", command_name(request), " fd ", fd);

		if (stats.enabled) {
			stats.ioctls++;
			switch (command_number(request)) {
			case command_number(DRM_IOCTL_PANTHOR_GROUP_SUBMIT):  stats.submits++; break;
			case command_number(DRM_IOCTL_PANTHOR_VM_BIND):       stats.binds++;   break;
			case command_number(DRM_IOCTL_PANTHOR_BO_CREATE):     stats.creates++; break;
			case command_number(DRM_IOCTL_GEM_CLOSE):             stats.closes++;  break;
			case command_number(DRM_IOCTL_SYNCOBJ_WAIT):
			case command_number(DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT): stats.waits++;   break;
			default: break;
			}
			stats.tick();
		}

		int ret = -1;

		if (fd != LIBDRM26_PANTHOR_FD || IOCGROUP(request) != DRM_IOCTL_BASE) {
			/* dma-buf and sync-file ioctls on descriptors of the table */
			errno = ENOSYS;
		} else {
			unsigned long const cmd = command_number(request);

			ret = (cmd >= DRM_COMMAND_BASE && cmd < DRM_COMMAND_END)
			    ? _panthor_ioctl((unsigned)(cmd - DRM_COMMAND_BASE), arg)
			    : _core_ioctl(request, arg);
		}

		if (ret && errno == ENOSYS)
			error("libdrm26: ", command_name(request), " (fd ", fd,
			      ") is not available with the panthor Gpu session");
		else if (ret && verbose)
			log("libdrm26: ", command_name(request), " failed, errno ", errno);

		return ret;
	}

	void *mmap(off_t offset, size_t length, int prot)
	{
		if ((uint64_t)offset == DRM_PANTHOR_USER_FLUSH_ID_MMIO_OFFSET) {

			Mutex_guard guard { _mutex };

			if (!_flush_id_page) {
				void * const page = ::mmap(nullptr, 4096, PROT_READ | PROT_WRITE,
				                           MAP_ANON | MAP_PRIVATE, -1, 0);
				if (page == MAP_FAILED)
					return MAP_FAILED;

				memset(page, 0, 4096);
				_flush_id_page = page;
			}
			return _flush_id_page;
		}

		(void)prot;

		uint32_t const handle = (uint32_t)((uint64_t)offset >> 12);

		Mutex_guard guard { _mutex };

		/* attached already: hand out the same address */
		void *known = nullptr;
		_mappings.for_each([&] (Mapping &mapping) {
			if (!known && mapping.handle == handle && mapping.ds.size() >= length) {
				mapping.users++;
				known = mapping.ds.local_addr<void>();
			}
		});
		if (known) {
			stats.reused++;
			return known;
		}

		stats.maps++;

		Dataspace_capability const cap =
			_gpu->map_cpu(Gpu::Vram_id { .value = handle },
			              Gpu::Mapping_attributes::rw());

		if (!cap.valid()) {
			error("libdrm26: map_cpu of buffer object ", handle, " failed");
			errno = EINVAL;
			return MAP_FAILED;
		}

		try {
			Mapping &mapping = *new (_heap) Mapping(_mappings, _env.rm(), handle, cap);

			if (mapping.ds.size() < length) {
				error("libdrm26: buffer object ", handle, " is smaller (",
				      mapping.ds.size(), ") than its mapping (", length, ")");
				destroy(_heap, &mapping);
				errno = EINVAL;
				return MAP_FAILED;
			}
			return mapping.ds.local_addr<void>();
		}
		catch (...) {
			error("libdrm26: could not attach buffer object ", handle);
			errno = ENOMEM;
			return MAP_FAILED;
		}
	}

	bool munmap(void *addr)
	{
		Mutex_guard guard { _mutex };

		/* stays for the life time of the process, Mesa maps it once */
		if (addr == _flush_id_page)
			return true;

		bool found = false;
		_mappings.for_each([&] (Mapping &mapping) {
			if (!found && mapping.ds.local_addr<void>() == addr) {
				if (mapping.users)
					mapping.users--;
				found = true;
			}
		});
		return found;
	}

	/**
	 * Wait for a sync file: import it into a temporary syncobj and wait
	 */
	int poll(struct pollfd &pfd, int timeout_ms)
	{
		pfd.revents = 0;

		if (!_table_fd(pfd.fd)) {
			pfd.revents = POLLNVAL;
			return 1;
		}

		drm_syncobj_create create { };
		if (_simple<drm_syncobj_create, Syncobj_create>(Op::SYNCOBJ_CREATE, create))
			return -1;

		drm_syncobj_handle import { };
		import.handle = create.handle;
		import.flags  = DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE;
		import.fd     = pfd.fd;

		int ret = _syncobj_fd_to_handle(import);

		if (!ret) {
			int64_t abs_ns = INT64_MAX;
			if (timeout_ms >= 0) {
				struct timespec now { };
				clock_gettime(CLOCK_MONOTONIC, &now);
				abs_ns = (int64_t)now.tv_sec*1000000000ll + now.tv_nsec
				       + (int64_t)timeout_ms*1000000ll;
				if (abs_ns <= 0) abs_ns = 1;
			}

			uint32_t first = 0;
			ret = _wait(&create.handle, nullptr, 1, abs_ns, 0, first, false);

			if (!ret) {
				pfd.revents = POLLIN;
				ret = 1;
			} else if (errno == ETIME) {
				ret = 0;
			}
		} else {
			pfd.revents = POLLNVAL;
			ret = 1;
		}

		int const saved_errno = errno;
		drm_syncobj_destroy destroy_arg { };
		destroy_arg.handle = create.handle;
		(void)_simple<drm_syncobj_destroy, Syncobj_destroy>(Op::SYNCOBJ_DESTROY, destroy_arg);
		errno = saved_errno;

		return ret;
	}

	int close(int fd)
	{
		/* the session stays open for the life time of the process */
		if (!_table_fd(fd))
			return 0;

		Mutex_guard guard { _mutex };

		Message msg = _message(Op::FD_CLOSE);
		Fd_close &p = msg.payload<Fd_close>();
		p.fd = _to_table_fd(fd);

		return _result(_execute(msg));
	}
};


static Genode::Constructible<Panthor_drm::Device> _device { };


static bool _device_ok()
{
	return _device.constructed() && _device->ok();
}


void panthor_drm_init()
{
	if (_device.constructed())
		return;

	/*
	 * The variable 'genode_env' of the application, if it has one and
	 * exports it (the eglut binding does, linked with --export-dynamic)
	 */
	Genode::Env ** const app_env =
		(Genode::Env **)dlsym(RTLD_DEFAULT, "genode_env");

	Genode::Env * const env = (app_env && *app_env) ? *app_env : vfs_gpu_env();
	if (!env) {
		Genode::error("libdrm26: no Genode environment: the application "
		              "defines no 'genode_env' and the VFS has no <gpu/>");
		return;
	}

	_device.construct(*env);
}


bool panthor_drm_owns_fd(int fd)
{
	return fd == LIBDRM26_PANTHOR_FD
	    || (fd >= LIBDRM26_FD_FIRST && fd <= LIBDRM26_FD_LAST);
}


int panthor_drm_ioctl(int fd, unsigned long request, void *arg)
{
	if (!_device_ok()) {
		errno = ENODEV;
		return -1;
	}
	return _device->ioctl(fd, request, arg);
}


void *panthor_drm_mmap(int fd, off_t offset, size_t length, int prot)
{
	if (!_device_ok() || fd != LIBDRM26_PANTHOR_FD) {
		errno = ENODEV;
		return MAP_FAILED;
	}
	return _device->mmap(offset, length, prot);
}


bool panthor_drm_munmap(void *addr, size_t)
{
	return _device_ok() && _device->munmap(addr);
}


int panthor_drm_poll(struct pollfd *fds, int timeout_ms)
{
	if (!_device_ok()) {
		errno = ENODEV;
		return -1;
	}
	return _device->poll(*fds, timeout_ms);
}


int panthor_drm_close(int fd)
{
	if (!_device_ok())
		return 0;

	return _device->close(fd);
}
