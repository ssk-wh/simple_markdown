#!/usr/bin/env python3
"""
Copy Qt DLLs to build/app directory for testing.
This allows the exe to find its dependencies without using installer/dist.
"""

import os
import shutil
from pathlib import Path

def find_qt_dir_from_cmake():
    """Find Qt directory from CMake cache."""
    cmake_cache = Path("build/CMakeCache.txt")
    if cmake_cache.exists():
        for line in cmake_cache.read_text().splitlines():
            if "Qt5Core_DIR" in line and "=" in line:
                val = line.split("=", 1)[1]
                qt_dir = Path(val).parent.parent.parent
                if qt_dir.exists():
                    return qt_dir
    return None

def copy_qt_dlls():
    build_app = Path("build/app")
    qt_dir = find_qt_dir_from_cmake()

    if not qt_dir:
        print("[ERROR] Qt directory not found!")
        return False

    qt_bin = qt_dir / "bin"
    print(f"[INFO] Qt directory: {qt_dir}")
    print(f"[INFO] Copying DLLs from: {qt_bin}")

    qt_dlls = [
        "Qt5Core.dll", "Qt5Gui.dll", "Qt5Widgets.dll",
        "Qt5Network.dll", "Qt5Svg.dll",
    ]

    copied = 0
    for dll in qt_dlls:
        src = qt_bin / dll
        dst = build_app / dll
        if src.exists():
            shutil.copy2(src, dst)
            print(f"  + {dll}")
            copied += 1
        else:
            print(f"  - {dll} not found")

    # Copy plugins
    qt_plugins = qt_dir / "plugins"
    for plugin_dir in ["platforms", "imageformats"]:
        src_dir = qt_plugins / plugin_dir
        if not src_dir.exists():
            continue
        dst_dir = build_app / plugin_dir
        dst_dir.mkdir(exist_ok=True)
        for dll in src_dir.glob("*.dll"):
            if not dll.stem.endswith("d"):  # skip debug
                shutil.copy2(dll, dst_dir / dll.name)
                copied += 1
        print(f"  + {plugin_dir}/ ({len(list(dst_dir.glob('*.dll')))} dlls)")

    print(f"\n[SUCCESS] Copied {copied} files")
    return True

if __name__ == "__main__":
    os.chdir(Path(__file__).parent)
    copy_qt_dlls()
