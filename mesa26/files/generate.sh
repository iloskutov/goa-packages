#!/bin/sh
#
# \brief  Produce the generated sources of Mesa with a meson build on the host
# \author Ivan Loskutov
# \date   2026-11-01
#
# Mesa generates part of its sources at build time: Python generators (NIR, GL
# API, formats, the panfrost pack headers and compilers) and, for panfrost,
# OpenCL C compiled by the host tools 'mesa_clc', 'vtn_bindgen2', and
# 'panfrost_compile' (library "libpan"), which need LLVM, clang, and the SPIR-V
# translator. Goa's build has none of them, so the import runs this script.
#
# Two build trees are used:
#
#   build-host  LLVM enabled, only the three host tools are built from it
#   build-gen   LLVM disabled, as the libraries of this project, with the tools
#               from build-host, only the files of <list> are built from it
#
# Host packages: meson, ninja, mako, pyyaml, packaging, LLVM, clang, libclc with
# SPIR-V, SPIRV-LLVM-Translator, SPIRV-Tools, SPIRV-Headers.
#
# usage: generate.sh <mesa-archive> <list> <dst-dir>
#
#   <mesa-archive>  the unpatched Mesa archive, the patches of this project do
#                   not touch an input of the generators
#   <list>          generated files, relative to the build tree
#   <dst-dir>       receives the files, with their paths
#
# The working directory receives the unpacked sources and the build trees.
#

set -e

ARCHIVE=$1
LIST=$2
DST=$3

# Goa's sandbox sets the PATH, Gentoo keeps LLVM in a directory per version
LLVM_CONFIG=$(command -v llvm-config ||
              ls -d /usr/lib/llvm/*/bin/llvm-config 2>/dev/null | sort -V | tail -n 1)
if [ -z "$LLVM_CONFIG" ]; then
	echo "Error: llvm-config not found" >&2
	exit 1
fi
PATH="$("$LLVM_CONFIG" --bindir):$PATH"

# the SPIR-V translator installs its pkg-config file next to LLVM's libraries
export PKG_CONFIG_PATH="$("$LLVM_CONFIG" --libdir)/pkgconfig:$PKG_CONFIG_PATH"

# generators iterate over sets of strings, keep their order stable (the order
# of the defines in 'mali_perf_*.c' still changes between runs, it has no effect)
export PYTHONHASHSEED=0

COMMON="-Dgallium-drivers=panfrost,softpipe -Dvulkan-drivers= -Dplatforms=
        -Dglx=disabled -Dglvnd=disabled -Degl=enabled -Dgles1=enabled
        -Dgles2=enabled -Dopengl=true -Dgbm=disabled -Dvideo-codecs=
        -Dgallium-va=disabled -Dvalgrind=disabled -Dlibunwind=disabled
        -Dzstd=disabled -Dlmsensors=disabled -Dshader-cache=disabled
        -Dxmlconfig=disabled -Dbuildtype=release --wrap-mode=nodownload"

WORK=$(pwd)
MESA=$WORK/mesa-host

mkdir -p "$MESA"
tar xf "$ARCHIVE" -C "$MESA" --strip-components=1

meson setup "$WORK/build-host" "$MESA" $COMMON \
      -Dllvm=enabled -Dshared-llvm=enabled

ninja --quiet -C "$WORK/build-host" src/compiler/clc/mesa_clc \
                                    src/compiler/spirv/vtn_bindgen2 \
                                    src/panfrost/clc/panfrost_compile

PATH="$WORK/build-host/src/compiler/clc:$WORK/build-host/src/compiler/spirv:$WORK/build-host/src/panfrost/clc:$PATH"
export PATH

meson setup "$WORK/build-gen" "$MESA" $COMMON \
      -Dllvm=disabled -Dmesa-clc=system -Dprecomp-compiler=system

ninja --quiet -C "$WORK/build-gen" $(cat "$LIST")

mkdir -p "$DST"
DST=$(cd "$DST" && pwd)
(cd "$WORK/build-gen" && cp --parents $(cat "$LIST") "$DST")
