#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: build_ffmpeg.sh /path/to/ffmpeg-4.4.2 /path/to/install"
    exit 2
fi

source_dir=$(cd "$1" && pwd)
mkdir -p "$2"
prefix=$(cd "$2" && pwd)
cd "$source_dir"

bash ./configure --prefix="$prefix" --toolchain=msvc --arch=x86_64 \
    --enable-shared --disable-static --disable-doc --disable-programs \
    --disable-autodetect --disable-network --disable-postproc --disable-debug \
    --disable-everything --enable-decoder=h264 --enable-parser=h264 \
    --enable-demuxer=h264 --enable-muxer=mp4 --enable-protocol=file

# These float functions are present in MSVC's UCRT. A transient link failure in
# the 4.4.2 configure probes can otherwise select conflicting fallback definitions.
sed -i -E 's/^(#define HAVE_(LDEXPF|LRINTF|ROUNDF)) 0$/\1 1/' config.h
make -j "${MHO_BUILD_JOBS:-8}"
make install
