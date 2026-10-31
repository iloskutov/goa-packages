#
# The parts of the panfrost driver that are independent of the GPU
# architecture version: device and kernel-interface code (pan_kmod), the
# Midgard and Bifrost/Valhall shader compilers, the gallium driver's common
# files, the command-stream decoder and the performance-counter tables.
#
include mk/panfrost26-common.inc
include mk/mesa26_list-panfrost.inc
