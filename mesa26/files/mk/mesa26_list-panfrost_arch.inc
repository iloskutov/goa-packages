#
# Generated from the host build of
# mesa-26.2.6, do not edit
#

INC_DIR += \
	$(MESA_GEN_DIR)/src/panfrost/genxml \
	$(MESA_SRC_DIR)/src/panfrost/genxml \
	$(MESA_GEN_DIR)/include \
	$(MESA_SRC_DIR)/include \
	$(MESA_GEN_DIR)/src \
	$(MESA_SRC_DIR)/src \
	$(MESA_GEN_DIR)/src/panfrost \
	$(MESA_SRC_DIR)/src/panfrost \
	$(MESA_GEN_DIR)/src/panfrost/shared \
	$(MESA_SRC_DIR)/src/panfrost/shared \
	$(MESA_GEN_DIR)/src/panfrost/compiler \
	$(MESA_SRC_DIR)/src/panfrost/compiler \
	$(MESA_GEN_DIR)/src/panfrost/lib \
	$(MESA_SRC_DIR)/src/panfrost/lib \
	$(MESA_GEN_DIR)/src/panfrost/libpan \
	$(MESA_SRC_DIR)/src/panfrost/libpan \
	$(MESA_GEN_DIR)/src/panfrost/model \
	$(MESA_SRC_DIR)/src/panfrost/model \
	$(MESA_GEN_DIR)/src/compiler/nir \
	$(MESA_SRC_DIR)/src/compiler/nir \
	$(MESA_GEN_DIR)/src/compiler \
	$(MESA_SRC_DIR)/src/compiler \
	$(MESA_GEN_DIR)/src/util \
	$(MESA_GEN_DIR)/src/util/format \
	$(MESA_GEN_DIR)/src/gallium/drivers/panfrost \
	$(MESA_SRC_DIR)/src/gallium/drivers/panfrost \
	$(MESA_SRC_DIR)/src/gallium/include \
	$(MESA_GEN_DIR)/src/gallium/auxiliary \
	$(MESA_SRC_DIR)/src/gallium/auxiliary \

SRC_C += \
	panfrost/genxml/decode.c \
	panfrost/genxml/decode_jm.c \
	panfrost/genxml/decode_csf.c \
	panfrost/lib/pan_format.c \
	panfrost/lib/pan_blend.c \
	panfrost/lib/pan_buffer.c \
	panfrost/lib/pan_desc.c \
	panfrost/lib/pan_fb.c \
	panfrost/lib/pan_fb_nir.c \
	panfrost/lib/pan_mod.c \
	panfrost/lib/pan_texture.c \
	panfrost/libpan/libpan_shaders_v$(PAN_ARCH).c \
	gallium/drivers/panfrost/pan_cmdstream.c \
	gallium/drivers/panfrost/pan_blend_cso.c \
	gallium/drivers/panfrost/pan_fb_preload.c \
	gallium/drivers/panfrost/pan_precomp.c \
	gallium/drivers/panfrost/pan_csf.c \

SRC_CC += \
	panfrost/libpan/libpan_v$(PAN_ARCH).cpp \

