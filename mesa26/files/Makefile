#
# The libraries in build order. Goa provides CC, CXX, CPPFLAGS, CFLAGS, CXXFLAGS,
# and CROSS_DEV_PREFIX. A library is built by lib.mk from its file in mk/.
#
PAN_VERSIONS := 4 5 6 7 9 10 12 13 14

LIBS := libdrm26 panfrost26 $(addprefix panfrost26_v,$(PAN_VERSIONS)) \
        mesa26 egl26 mesa26_gpu-panfrost mesa26_gpu-softpipe

all: $(LIBS)

# serial at this level, each library builds its objects in parallel
.NOTPARALLEL:

$(LIBS):
	$(MAKE) -f lib.mk LIB=$@

.PHONY: all $(LIBS)
