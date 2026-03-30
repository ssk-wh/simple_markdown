#!/usr/bin/env python3
"""
SimpleMarkdown dist collection script.
Collects exe, Qt dependencies, Qt plugins, and MSVC runtime into installer/dist.

Usage: python collect_dist.py [build_dir]
Default build_dir = ../build/app/Release
"""

import os
import sys
import shutil
import subprocess
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_DIR = SCRIPT_DIR.parent
DIST_DIR = SCRIPT_DIR / "dist"
EXE_NAME = "SimpleMarkdown.exe"

QT_DLLS = [
    "Qt5Core.dll", "Qt5Gui.dll", "Qt5Widgets.dll",
    "Qt5Network.dll", "Qt5Svg.dll",
]

QT_PLUGINS = ["platforms", "imageformats", "styles"]

VCRT_DLLS = ["MSVCP140.dll", "VCRUNTIME140.dll", "VCRUNTIME140_1.dll"]


def find_qt_dir_from_cmake():
    """Find Qt directory from CMake cache (most reliable)."""
    cmake_cache = PROJECT_DIR / "build" / "CMakeCache.txt"
    if cmake_cache.exists():
        for line in cmake_cache.read_text().splitlines():
            if "Qt5Core_DIR" in line and "=" in line:
                # e.g. Qt5Core_DIR:PATH=D:/Qt/Qt5.12.9/5.12.9/msvc2017_64/lib/cmake/Qt5Core
                val = line.split("=", 1)[1]
                qt_dir = Path(val).parent.parent.parent  # up from lib/cmake/Qt5Core
                if qt_dir.exists():
                    return qt_dir
    return None


def find_qt_dir():
    """Find Qt installation directory, preferring CMake cache."""
    # First: use CMake cache (matches the actual build)
    qt_dir = find_qt_dir_from_cmake()
    if qt_dir:
        return qt_dir

    # Fallback: qmake in PATH
    try:
        qmake = shutil.which("qmake")
        if qmake:
            return Path(qmake).resolve().parent.parent
    except Exception:
        pass

    # Last resort: check common locations
    for base in [r"C:\Qt", r"D:\Qt"]:
        p = Path(base)
        if p.exists():
            for ver_dir in sorted(p.iterdir(), reverse=True):
                for compiler in ["msvc2019_64", "msvc2017_64", "msvc2019", "msvc2017"]:
                    candidate = ver_dir / compiler
                    if (candidate / "bin" / "qmake.exe").exists():
                        return candidate
    return None


def find_vcrt_dll(name):
    """Find MSVC runtime DLL."""
    # Check Visual Studio installations
    vs_paths = [
        r"C:\Program Files\Microsoft Visual Studio",
        r"C:\Program Files (x86)\Microsoft Visual Studio",
    ]
    for vs_base in vs_paths:
        if not os.path.exists(vs_base):
            continue
        for root, dirs, files in os.walk(vs_base):
            if name.lower() in [f.lower() for f in files]:
                # Prefer x86 (32-bit) for x86 builds, x64 for x64
                full = os.path.join(root, name)
                if os.path.isfile(full) and "onecore" not in root.lower() and "arm" not in root.lower():
                    return full

    # Fallback: system directory
    sys32 = Path(r"C:\windows\system32") / name
    if sys32.exists():
        return str(sys32)
    return None


def is_debug_dll(filepath):
    """Check if a DLL is a debug version (ends with 'd.dll' and non-debug exists)."""
    name = filepath.stem
    if name.endswith("d") and len(name) > 1:
        non_debug = filepath.parent / (name[:-1] + ".dll")
        return non_debug.exists()
    return False


def collect(build_dir):
    exe_path = build_dir / EXE_NAME

    if not exe_path.exists():
        print(f"[ERROR] {exe_path} not found. Build Release first.")
        return False

    print("=" * 50)
    print("  SimpleMarkdown dist collection")
    print("=" * 50)
    print()

    # Step 1: Clean dist
    print("[1/5] Cleaning dist directory...")
    if DIST_DIR.exists():
        shutil.rmtree(DIST_DIR)
    DIST_DIR.mkdir(parents=True)

    # Step 2: Copy exe and documentation
    print(f"[2/5] Copying {EXE_NAME} and documentation...")
    shutil.copy2(exe_path, DIST_DIR / EXE_NAME)

    # Copy CHANGELOG.md if it exists
    changelog = PROJECT_DIR / "CHANGELOG.md"
    if changelog.exists():
        shutil.copy2(changelog, DIST_DIR / "CHANGELOG.md")
        print(f"  + CHANGELOG.md")

    # Step 3: Collect Qt dependencies
    print("[3/5] Collecting Qt dependencies...")
    qt_dir = find_qt_dir()
    if qt_dir is None:
        print("  [ERROR] Qt directory not found!")
        return False

    qt_bin = qt_dir / "bin"
    qt_plugins = qt_dir / "plugins"
    print(f"  Qt directory: {qt_dir}")

    # Copy Qt DLLs
    for dll in QT_DLLS:
        src = qt_bin / dll
        if src.exists():
            shutil.copy2(src, DIST_DIR / dll)
            print(f"  + {dll}")
        else:
            print(f"  [WARN] {dll} not found")

    # Copy Qt plugins
    for plugin_dir in QT_PLUGINS:
        src_dir = qt_plugins / plugin_dir
        if not src_dir.exists():
            print(f"  [WARN] Plugin dir {plugin_dir} not found")
            continue

        dst_dir = DIST_DIR / plugin_dir
        dst_dir.mkdir(exist_ok=True)
        count = 0
        for dll in src_dir.glob("*.dll"):
            if not is_debug_dll(dll):
                shutil.copy2(dll, dst_dir / dll.name)
                count += 1
        print(f"  + {plugin_dir}/ ({count} files)")

    # Step 4: Copy build directory DLLs (in case cmake copies Qt DLLs there)
    print("[4/5] Checking build directory for extra DLLs...")
    for dll in build_dir.glob("*.dll"):
        dst = DIST_DIR / dll.name
        if not dst.exists():
            shutil.copy2(dll, dst)
            print(f"  + {dll.name} (from build dir)")

    # Step 5: Collect MSVC runtime
    print("[5/5] Collecting MSVC runtime...")
    for vcrt in VCRT_DLLS:
        # Skip if already collected
        if (DIST_DIR / vcrt).exists():
            print(f"  = {vcrt} (already present)")
            continue
        path = find_vcrt_dll(vcrt)
        if path:
            shutil.copy2(path, DIST_DIR / vcrt)
            print(f"  + {vcrt}")
        else:
            print(f"  [WARN] {vcrt} not found!")

    # Clean debug DLLs that may have slipped in
    cleaned = 0
    for dll in DIST_DIR.rglob("*.dll"):
        if is_debug_dll(dll):
            dll.unlink()
            cleaned += 1
    if cleaned:
        print(f"  Cleaned {cleaned} debug DLLs")

    # Summary
    print()
    print("=" * 50)
    print("  Collection complete!")
    print("=" * 50)
    print()
    print(f"  dist: {DIST_DIR}")

    total_size = 0
    file_count = 0
    for f in DIST_DIR.rglob("*"):
        if f.is_file():
            file_count += 1
            total_size += f.stat().st_size

    print(f"  Files: {file_count}")
    print(f"  Total size: {total_size / 1024 / 1024:.1f} MB")
    print()

    # List all files
    for f in sorted(DIST_DIR.rglob("*")):
        if f.is_file():
            rel = f.relative_to(DIST_DIR)
            size_kb = f.stat().st_size / 1024
            print(f"    {rel} ({size_kb:.0f} KB)")

    print()
    return True


if __name__ == "__main__":
    build_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else PROJECT_DIR / "build" / "app" / "Release"
    build_dir = build_dir.resolve()

    if not collect(build_dir):
        sys.exit(1)
