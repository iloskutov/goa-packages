#
# glmark2 linked against Mesa 26. Goa provides CC, CXX, CPPFLAGS, CFLAGS, and
# CXXFLAGS.
#
TARGET := glmark2_26
ASSETS := glmark2_assets.tar

SRC_CC := a.cc base-renderer.cpp benchmark-collection.cpp benchmark.cpp \
          blur-renderer.cpp canvas-generic.cpp copy-renderer.cpp d.cc e.cc \
          gl-headers.cpp gl-state-egl.cpp gl-visual-config.cpp i.cc \
          image-reader.cpp lamp.cc log.cc logo.cc luminance-renderer.cpp \
          main.cpp main-loop.cpp mat.cc m.cc mesh.cpp model.cpp n.cc \
          normal-from-height-renderer.cpp o.cc options.cpp overlay-renderer.cpp \
          program.cc renderer-chain.cpp results-file.cpp s.cc scene-buffer.cpp \
          scene-build.cpp scene-bump.cpp scene-clear.cpp scene-conditionals.cpp \
          scene.cpp scene-default-options.cpp scene-desktop.cpp \
          scene-effect-2d.cpp scene-function.cpp scene-grid.cpp scene-ideas.cpp \
          scene-jellyfish.cpp scene-loop.cpp scene-pulsar.cpp scene-refract.cpp \
          scene-shading.cpp scene-shadow.cpp scene-terrain.cpp scene-texture.cpp \
          shader-source.cc simplex-noise-renderer.cpp splines.cc table.cc t.cc \
          terrain-renderer.cpp text-renderer.cpp texture.cpp texture-renderer.cpp \
          util.cc native-state-genode.cc shared_library_26.cc

SRC_C := dummies.c egl.c gles2.c

G := glmark2/src

VPATH := glue $(G) $(G)/glad/src $(G)/libmatrix $(G)/scene-ideas $(G)/scene-terrain

INC := -Iglue -I$(G) -I$(G)/glad/include -I$(G)/libmatrix -I$(G)/scene-ideas \
       -I$(G)/scene-terrain

DEFS := -DUSE_EXCEPTIONS -DGLMARK2_USE_GLESv2 -DGLMARK2_USE_EGL \
        -DGLMARK_VERSION='"2023.01"' -DGLMARK_DATA_PATH='"/data"' \
        -D__GENODE__ -DGLMARK2_USE_GENODE -DGLMARK2_EXECUTABLE='"glmark2"'

OBJS := $(addprefix obj/,$(addsuffix .o,$(basename $(SRC_CC) $(SRC_C))))

all: $(TARGET) $(ASSETS)

obj/%.o: %.c
	@mkdir -p obj
	$(CC) $(INC) $(CPPFLAGS) $(CFLAGS) $(DEFS) -c $< -o $@

obj/%.o: %.cc
	@mkdir -p obj
	$(CXX) $(INC) $(CPPFLAGS) $(CXXFLAGS) $(DEFS) -c $< -o $@

obj/%.o: %.cpp
	@mkdir -p obj
	$(CXX) $(INC) $(CPPFLAGS) $(CXXFLAGS) $(DEFS) -c $< -o $@

# glmark2 exports 'genode_env' for the back end of libdrm26
$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -Wl,--export-dynamic -o $@

$(ASSETS):
	tar -chf $@ --sort=name --owner=0 --group=0 --mtime=@0 \
	    -C glmark2/data models shaders textures
