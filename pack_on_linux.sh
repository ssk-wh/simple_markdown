#!/bin/bash
set -e

# ===============================================
# SimpleMarkdown - Linux Pack Script
# [Spec specs/30-构建与发布流程]：自动先调用 build_on_linux.sh release
# 用法: ./pack_on_linux.sh  (无需先手动 build)
# ===============================================

cd "$(dirname "$0")"

BUILD_DIR="build"

echo "================================================"
echo "  SimpleMarkdown Linux Pack"
echo "================================================"

# Step 0: 编译
echo "[0/2] Building Release..."
./build_on_linux.sh release || { echo "[ERROR] Build failed! Aborting pack."; exit 1; }

# 验证编译产物
if [ ! -f "$BUILD_DIR/src/app/SimpleMarkdown" ]; then
    echo "[ERROR] $BUILD_DIR/src/app/SimpleMarkdown not found after build."
    exit 1
fi

# Step 1: 从 CHANGELOG.md 生成 debian/changelog
echo "[1/2] Generating debian/changelog from CHANGELOG.md..."
python3 scripts/gen_debian_changelog.py
echo "      Done."

# Step 2: 构建 deb 包
echo "[2/2] Building deb package..."
dpkg-buildpackage -us -uc -b
echo ""
echo "================================================"
echo "  SUCCESS: Pack completed!"
echo "================================================"
echo ""
echo "Package location:"
ls -l ../simplemarkdown_*.deb 2>/dev/null || echo "  (deb file is in parent directory)"
echo ""
