#
# On arm_v8, mesa26.lib.so contains the panfrost driver, which reaches the
# kernel driver through libdrm26 (the stock port builds lima the same way).
#
# MESA26_SOFTPIPE_ONLY=yes leaves the panfrost libraries out (shorter build
# while working on the core).
#
LIBS   += libdrm26
CC_OPT += -DHAVE_LIBDRM

ifneq ($(MESA26_SOFTPIPE_ONLY),yes)
LIBS   += panfrost26 $(addprefix panfrost26_v,4 5 6 7 9 10 12 13 14)
CC_OPT += -DHAVE_PANFROST

MESA26_DRIVER_DEFS += -DGALLIUM_PANFROST
endif

INC_DIR += lib/libdrm lib/libdrm/include lib/libdrm/include/drm
include mk/mesa26.inc

#
# What upstream's build adds for aarch64 and the x86_64 host build that the
# lists come from therefore lacks: two sources and the FP-state builtins
# (util/u_math.c).
#
SRC_C  += util/blake3/blake3_neon.c util/cache_ops_aarch64.c
CC_OPT += -DHAVE___BUILTIN_AARCH64_GET_FPCR -DHAVE___BUILTIN_AARCH64_SET_FPCR
