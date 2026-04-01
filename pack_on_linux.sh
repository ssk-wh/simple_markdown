#!/bin/bash
set -e

# ===============================================
# SimpleMarkdown - Linux Pack Script
# 用法: 先运行 build_on_linux.sh，再运行本脚本
# ===============================================

cd "$(dirname "$0")"

BUILD_DIR="build"

# 检查编译产物
if [ ! -f "$BUILD_DIR/src/app/SimpleMarkdown" ]; then
    echo "[ERROR] $BUILD_DIR/src/app/SimpleMarkdown not found."
    echo "        Please run build_on_linux.sh first."
    exit 1
fi

echo "================================================"
echo "  SimpleMarkdown Linux Pack"
echo "================================================"

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
