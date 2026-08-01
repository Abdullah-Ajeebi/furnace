#!/bin/bash
set -e

BUILD_DIR="build"

#echo ">>> Syncing latest official source from tildearrow/furnace..."
#git remote add upstream https://github.com/tildearrow/furnace.git 2>/dev/null || true
#git fetch upstream
#git checkout -B upstream-master upstream/master
#git submodule update --init --recursive

if [ ! -d "$BUILD_DIR" ]; then
    echo ">>> Initializing CMake build directory..."
    mkdir "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake .. -G "Ninja" \
             -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG -std=c++14 -march=native -mtune=native"
else
    echo ">>> Reusing CMake cache for incremental build..."
    cd "$BUILD_DIR"
fi

echo ">>> Compiling official Furnace Tracker on 4 parallel threads..."
cmake --build . --parallel 4