#
# Mesa 26.2.6, second Mesa next to the stock one of
# libports. All library names carry '26' so that both can be built in one
# build directory and loaded into one system image.
#

MESA_SRC_DIR  := lib/mesa26
MESA_GEN_DIR  := generated
MESA26_DIR    := glue

INC_DIR += $(MESA26_DIR)/include \
           $(MESA_SRC_DIR)/include \
           $(MESA_SRC_DIR)/src \
           $(MESA_GEN_DIR)/src \
           $(MESA_SRC_DIR)/src/gallium/include \
           $(MESA_SRC_DIR)/src/gallium/auxiliary

#
# The defines of the host's meson configuration, without what is specific to
# Linux, glibc
# and x86: KCMP, sched affinity, futex, udmabuf, memfd, inotify, shm, udev,
# libdisplay-info, libelf, OpenMP, SPIRV-Tools, renderdoc, SSE4.1, clflushopt,
# program_invocation_name, secure_getenv, GNU qsort_r, reallocarray,
# issignaling, strtod_l, endian.h, sysmacros.
#
CC_OPT += -D__GENODE__ \
          -DHAVE_GENODE_PLATFORM \
          -DPACKAGE_VERSION='"26.2.6"' \
          -DPACKAGE_BUGREPORT='"https://gitlab.freedesktop.org/mesa/mesa/-/issues"' \
          -DNDEBUG \
          -DMESA_DEBUG=0 \
          -D__STDC_CONSTANT_MACROS \
          -D__STDC_FORMAT_MACROS \
          -D__STDC_LIMIT_MACROS \
          -DHAVE_OPENGL=1 \
          -DHAVE_OPENGL_ES_1=1 \
          -DHAVE_OPENGL_ES_2=1 \
          -DHAVE_GFX_COMPUTE \
          -DHAVE_SOFTPIPE \
          -DHAVE_SWRAST \
          -DHAVE_DRI \
          -DHAVE_DRI2 \
          -DMESA_SYSTEM_HAS_KMS_DRM=0 \
          -DUSE_LIBGLVND=0 \
          -DGLAPI_EXPORT_PROTO_ENTRY_POINTS=1 \
          -DDRAW_LLVM_AVAILABLE=0 \
          -DAMD_LLVM_AVAILABLE=0 \
          -DGALLIVM_USE_ORCJIT=0 \
          -DTHREAD_SANITIZER=0 \
          -DHAVE_RENDERDOC_INTEGRATION=0 \
          -DVIDEO_CODEC_VC1DEC=0 \
          -DVIDEO_CODEC_H264DEC=0 \
          -DVIDEO_CODEC_H264ENC=0 \
          -DVIDEO_CODEC_H265DEC=0 \
          -DVIDEO_CODEC_H265ENC=0 \
          -DVIDEO_CODEC_AV1DEC=0 \
          -DVIDEO_CODEC_AV1ENC=0 \
          -DVIDEO_CODEC_VP9DEC=0 \
          -DVIDEO_CODEC_MPEG12DEC=0 \
          -DVIDEO_CODEC_JPEGDEC=0 \
          -DHAVE___BUILTIN_BSWAP32 \
          -DHAVE___BUILTIN_BSWAP64 \
          -DHAVE___BUILTIN_CLZ \
          -DHAVE___BUILTIN_CLZLL \
          -DHAVE___BUILTIN_CTZ \
          -DHAVE___BUILTIN_EXPECT \
          -DHAVE___BUILTIN_FFS \
          -DHAVE___BUILTIN_FFSLL \
          -DHAVE___BUILTIN_POPCOUNT \
          -DHAVE___BUILTIN_POPCOUNTLL \
          -DHAVE___BUILTIN_UNREACHABLE \
          -DHAVE___BUILTIN_TYPES_COMPATIBLE_P \
          -DHAVE___BUILTIN_ADD_OVERFLOW \
          -DHAVE___BUILTIN_ADD_OVERFLOW_P \
          -DHAVE___BUILTIN_SUB_OVERFLOW_P \
          -DHAVE_FUNC_ATTRIBUTE_ALIAS \
          -DHAVE_FUNC_ATTRIBUTE_COLD \
          -DHAVE_FUNC_ATTRIBUTE_CONST \
          -DHAVE_FUNC_ATTRIBUTE_FLATTEN \
          -DHAVE_FUNC_ATTRIBUTE_FORMAT \
          -DHAVE_FUNC_ATTRIBUTE_MALLOC \
          -DHAVE_FUNC_ATTRIBUTE_NORETURN \
          -DHAVE_FUNC_ATTRIBUTE_OPTIMIZE \
          -DHAVE_FUNC_ATTRIBUTE_PACKED \
          -DHAVE_FUNC_ATTRIBUTE_PURE \
          -DHAVE_FUNC_ATTRIBUTE_RETURNS_NONNULL \
          -DHAVE_FUNC_ATTRIBUTE_UNUSED \
          -DHAVE_FUNC_ATTRIBUTE_VISIBILITY \
          -DHAVE_FUNC_ATTRIBUTE_WARN_UNUSED_RESULT \
          -DHAVE_FUNC_ATTRIBUTE_WEAK \
          -DHAVE_UINT128 \
          -DUSE_GCC_ATOMIC_BUILTINS \
          -DHAVE_PTHREAD \
          -DHAVE_PTHREAD_NP_H \
          -DHAVE_DLFCN_H \
          -DHAVE_DLADDR \
          -DHAVE_DL_ITERATE_PHDR \
          -DHAVE_DIRENT_D_TYPE \
          -DHAVE_FLOCK \
          -DHAVE_GETRANDOM \
          -DHAVE_MKOSTEMP \
          -DHAVE_POLL_H \
          -DHAVE_POSIX_MEMALIGN \
          -DHAVE_RANDOM_R \
          -DHAVE_STRTOF \
          -DHAVE_STRTOK_R \
          -DHAVE_STRUCT_TIMESPEC \
          -DHAVE_TIMESPEC_GET \
          -DHAVE_SYSCONF \
          -DHAVE_ZLIB \
          -DHAVE_COMPRESSION \
          -DXXH_FORCE_ALIGN_CHECK=0 \
          -DXXH_FORCE_MEMORY_ACCESS=0

CC_OPT += -fno-math-errno -fno-trapping-math

# see src/lib/mesa26/include
CC_OPT += -include mesa26_compat.h

#
# Otherwise TLS init function calls will be created, for example, for
# '_mesa_glapi_tls_Context' in >C++11 code. This requires support in the
# dynamic linker which we don't have (as in the stock Mesa port).
#
CC_CXX_OPT += -fno-extern-tls-init

#
# As upstream's build: Mesa's GLSL IR stores the allocation context into an
# object in 'operator new', before the constructor runs. GCC's default
# lifetime analysis removes such stores (the compiler then crashes in
# 'linear_zalloc_child' with a null context).
#
CC_CXX_OPT += -flifetime-dse=1

CC_CXX_OPT_STD := -std=gnu++17
CC_C_OPT       += -std=gnu11

CC_CXX_OPT += -Wno-deprecated-enum-enum-conversion
CC_OPT     += -Wno-nonnull-compare -Wno-unused-function


vpath %.c   $(MESA_SRC_DIR)/src
vpath %.c   $(MESA_GEN_DIR)/src
vpath %.cpp $(MESA_SRC_DIR)/src
vpath %.cpp $(MESA_GEN_DIR)/src

