#!/bin/bash
set -e

BUILD_DIR="build"
BUILD_TYPE="Release"
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# 解析参数
for arg in "$@"; do
    case "$arg" in
        debug)   BUILD_TYPE="Debug" ;;
        release) BUILD_TYPE="Release" ;;
        clean)   rm -rf "$BUILD_DIR"; echo "Cleaned."; exit 0 ;;
        *)       echo "Unknown option: $arg"; exit 1 ;;
    esac
done

echo "================================================"
echo "  SimpleMarkdown Build ($BUILD_TYPE)"
echo "================================================"

# Configure (仅首次或缓存被清除时)
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "[1/2] CMake configure..."
    cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
else
    echo "[1/2] CMake cache exists, skipping configure."
fi

# Build
echo "[2/2] Building (-j$JOBS)..."
cmake --build "$BUILD_DIR" -j "$JOBS"

if [ -f "$BUILD_DIR/src/app/SimpleMarkdown" ]; then
    SIZE=$(stat -c%s "$BUILD_DIR/src/app/SimpleMarkdown" 2>/dev/null || stat -f%z "$BUILD_DIR/src/app/SimpleMarkdown" 2>/dev/null || echo "?")
    echo "================================================"
    echo "  Build succeeded: $BUILD_DIR/src/app/SimpleMarkdown"
    echo "  Size: $SIZE bytes"
    echo "================================================"
else
    echo "[ERROR] Build completed but executable not found!"
    exit 1
fi
