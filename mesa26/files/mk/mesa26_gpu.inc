include mk/mesa26-common.inc

INC_DIR += $(MESA_SRC_DIR)/src/egl/main \
           $(MESA_SRC_DIR)/src/egl/drivers/dri2 \
           $(MESA_SRC_DIR)/src/gallium/frontends/dri \
           $(MESA_SRC_DIR)/src/loader \
           $(MESA_SRC_DIR)/src/mesa \
           $(MESA_GEN_DIR)/src/mesa \
           $(MESA_GEN_DIR)/src/util

