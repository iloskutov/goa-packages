#
# Build one library: LIB names the file mk/$(LIB).mk, which sets SRC_C, SRC_CC,
# INC_DIR, CC_OPT, and the per-file options CC_OPT_<source without suffix>.
#
include mk/$(LIB).mk

# libraries of this project a library needs, the others come from the APIs
SO_DEPS := $(filter mesa26 egl26 libdrm26,$(LIBS))
AR_DEPS := $(filter panfrost%,$(LIBS))

O    := obj/$(LIB)
OBJS := $(addprefix $(O)/,$(addsuffix .o,$(basename $(SRC_C) $(SRC_CC))))
INC  := $(addprefix -I,$(INC_DIR))

ifeq ($(SHARED_LIB),yes)
TARGET := $(LIB).lib.so
else
TARGET := lib$(LIB).a
endif

all: $(TARGET)

$(O)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(INC) $(CPPFLAGS) $(CFLAGS) $(CC_C_OPT) $(CC_OPT) $(CC_OPT_$*) -c $< -o $@

$(O)/%.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(INC) $(CPPFLAGS) $(CXXFLAGS) $(CC_CXX_OPT_STD) $(CC_CXX_OPT) $(CC_OPT) $(CC_OPT_$*) -c $< -o $@

$(O)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(INC) $(CPPFLAGS) $(CXXFLAGS) $(CC_CXX_OPT_STD) $(CC_CXX_OPT) $(CC_OPT) $(CC_OPT_$*) -c $< -o $@

lib$(LIB).a: $(OBJS)
	rm -f $@
	$(file >$(O).rsp,$(OBJS))
	$(CROSS_DEV_PREFIX)ar rcs $@ @$(O).rsp

$(LIB).lib.so: $(OBJS) $(addsuffix .lib.so,$(SO_DEPS)) $(addprefix lib,$(addsuffix .a,$(AR_DEPS)))
	$(file >$(O).rsp,$(OBJS))
	$(CXX) -shared -Wl,-soname=$@ @$(O).rsp \
	       -Wl,--whole-archive $(addprefix -l:lib,$(addsuffix .a,$(AR_DEPS))) -Wl,--no-whole-archive \
	       -L. $(addprefix -l:,$(addsuffix .lib.so,$(SO_DEPS))) -o $@
