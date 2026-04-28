#!/usr/bin/env python3
# coding: utf-8
"""
SimpleMarkdown 渲染端到端自动化测试驱动。

默认流程:
  0. 先调 tools/tests/_build.bat 做 ENABLE_TEST_MODE 增量编译（--no-build 可跳过）
  1. 删除旧的 <TEMP>/render_blocks.json（避免读到上一次结果）
  2. taskkill 已存在的 SimpleMarkdown 实例（绕开单实例参数转发）
  3. 启动 SimpleMarkdown.exe <sample.md>
  4. 轮询 <TEMP>/render_blocks.json 直到出现且 mtime > 启动时间（最长 --timeout 秒）
  5. taskkill 进程
  6. 调 verify_render.py 校验 → 收集 PASS/FAIL

用法:
  python tools/tests/run_all.py                    # 默认：先 build 再跑
  python tools/tests/run_all.py --no-build         # 跳过 build（仅迭代 verify 规则时用）
  python tools/tests/run_all.py --clean-build      # 清 CMakeCache 后全量重建再跑
  python tools/tests/run_all.py --sample tools/tests/regression_list_zoom.md
  python tools/tests/run_all.py --theme both       # light + dark 各跑一遍
  python tools/tests/run_all.py --keep-json        # 保留每个样本的 JSON 副本到 tmp/

退出码: 全过 0；任意样本失败 = 失败样本数（capped to 99）；build / probe 失败 = 99
"""

import argparse
import contextlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

# Windows GBK 控制台无法直接打印 UTF-8 中文 → 用 errors='replace' 兜底防 crash
if sys.stdout.encoding and sys.stdout.encoding.lower() not in ("utf-8", "utf8"):
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
        sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, OSError):
        pass

ROOT = Path(__file__).resolve().parents[2]  # 项目根
TESTS_DIR = Path(__file__).resolve().parent  # tools/tests/
VERIFY_PY = TESTS_DIR / "verify_render.py"
RENDER_JSON = Path(tempfile.gettempdir()) / "render_blocks.json"

IS_WIN = platform.system() == "Windows"
EXE_NAME = "SimpleMarkdown.exe" if IS_WIN else "SimpleMarkdown"

# QSettings 持久化位置（Windows 注册表）
# main.cpp: setOrganizationName("SimpleMarkdown") + setApplicationName("SimpleMarkdown")
# MainWindow::saveSettings 用 key "view/themeMode"，QSettings 把 "/" 翻译为子键
QSETTINGS_VIEW_KEY = r"Software\SimpleMarkdown\SimpleMarkdown\view"
THEME_VALUE_NAME = "themeMode"
SUPPORTED_THEMES = ("light", "dark", "follow_system")

# QSettings 类型映射（实测自 reg query：Qt 把 int 存 REG_DWORD，bool/double/string 存 REG_SZ）
# 见 specs/模块-app/12-主题插件系统.md / MainWindow::saveSettings
ZOOM_PRESET = (-4, 0, 4, 10)         # fontSizeDelta sweep 档位
SPACING_PRESET = (1.0, 1.5, 2.0)     # lineSpacing sweep 档位
SPACING_VALID = (1.0, 1.2, 1.5, 1.8, 2.0)  # 与 MainWindow::createMenu 中的 spacings[] 对齐

# build 出口候选（按优先级，build_on_win.bat 用 NMake 输出到 build/src/app/）
EXE_CANDIDATES = [
    ROOT / "build" / "src" / "app" / EXE_NAME,
    ROOT / "build" / "src" / "app" / "Release" / EXE_NAME,
    ROOT / "build" / "src" / "app" / "Debug" / EXE_NAME,
    ROOT / "build" / "src" / "Release" / EXE_NAME,
    ROOT / "build" / "src" / "Debug" / EXE_NAME,
    ROOT / "build" / "src" / EXE_NAME,
    ROOT / "build" / "bin" / EXE_NAME,
    ROOT / "build" / EXE_NAME,
]


def build_with_test_mode(clean: bool = False) -> None:
    """触发 ENABLE_TEST_MODE 增量构建。失败抛 RuntimeError。

    Windows: 调 tools/tests/_build.bat（首次缺失会自动用 _make_build_bat.py 生成）。
    Linux/macOS: 直接调 cmake configure + cmake --build。
    """
    if not IS_WIN:
        build_dir = ROOT / "build"
        if clean:
            cache = build_dir / "CMakeCache.txt"
            cmake_files = build_dir / "CMakeFiles"
            if cache.exists():
                cache.unlink()
            if cmake_files.exists():
                shutil.rmtree(cmake_files)
        print("[build] cmake configure with -DENABLE_TEST_MODE=ON ...")
        r = subprocess.run(
            ["cmake", "-S", str(ROOT), "-B", str(build_dir),
             "-DCMAKE_BUILD_TYPE=Release", "-DENABLE_TEST_MODE=ON"],
            cwd=str(ROOT),
        )
        if r.returncode != 0:
            raise RuntimeError(f"cmake configure failed (exit {r.returncode})")
        print("[build] cmake --build ...")
        r = subprocess.run(["cmake", "--build", str(build_dir), "-j"], cwd=str(ROOT))
        if r.returncode != 0:
            raise RuntimeError(f"cmake build failed (exit {r.returncode})")
        print("[build] OK")
        return

    # Windows: 调 _build.bat
    bat = TESTS_DIR / "_build.bat"
    gen = TESTS_DIR / "_make_build_bat.py"
    if not bat.exists() and gen.exists():
        print("[build] _build.bat 不存在，正在用 _make_build_bat.py 生成 ...")
        subprocess.run([sys.executable, str(gen)], check=True)
    if not bat.exists():
        raise RuntimeError(f"_build.bat 不存在: {bat}")
    cmd = [r"C:\Windows\System32\cmd.exe", "/c", str(bat)]
    if clean:
        cmd.append("--clean")
    print(f"[build] running _build.bat{' --clean' if clean else ''} (this may take 1-3 min for incremental, 5-8 min for clean) ...")
    r = subprocess.run(cmd)
    if r.returncode != 0:
        raise RuntimeError(f"_build.bat failed (exit {r.returncode}). 检查 _build.bat 头部路径常量是否匹配本机环境")
    print("[build] OK")


@contextlib.contextmanager
def setting_override(value_name: str, new_value, reg_type):
    """通用 contextmanager：临时改 HKCU\\...\\view\\<value_name> 为 new_value，退出时还原原值（含原类型）。

    若 new_value 为 None 则 no-op（沿用当前设置）。仅 Windows 有效。
    """
    if new_value is None or not IS_WIN:
        yield
        return

    import winreg

    original_value = None
    original_type = None
    key_existed = False

    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, QSETTINGS_VIEW_KEY, 0, winreg.KEY_READ) as k:
            try:
                original_value, original_type = winreg.QueryValueEx(k, value_name)
                key_existed = True
            except FileNotFoundError:
                pass
    except FileNotFoundError:
        pass

    with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, QSETTINGS_VIEW_KEY, 0, winreg.KEY_ALL_ACCESS) as k:
        winreg.SetValueEx(k, value_name, 0, reg_type, new_value)

    try:
        yield
    finally:
        with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, QSETTINGS_VIEW_KEY, 0, winreg.KEY_ALL_ACCESS) as k:
            if key_existed:
                winreg.SetValueEx(k, value_name, 0, original_type, original_value)
            else:
                with contextlib.suppress(FileNotFoundError):
                    winreg.DeleteValue(k, value_name)


@contextlib.contextmanager
def theme_override(theme):
    """切换 view/themeMode（REG_SZ）。"""
    if theme is None:
        yield
        return
    if theme not in SUPPORTED_THEMES:
        raise ValueError(f"theme 必须是 {SUPPORTED_THEMES} 之一，收到 {theme!r}")
    with setting_override(THEME_VALUE_NAME, theme, _reg_sz()):
        yield


@contextlib.contextmanager
def zoom_override(delta):
    """切换 view/fontSizeDelta（REG_DWORD）。范围 [-8, 20]，步长 2（与 MainWindow 一致）。

    REG_DWORD 是 unsigned 32-bit；Qt 把负数 int 写为 two's complement，
    所以 winreg 也要把负数转成 unsigned 等价形式。
    """
    if delta is None:
        yield
        return
    if not isinstance(delta, int) or delta < -8 or delta > 20 or delta % 2 != 0:
        raise ValueError(f"fontSizeDelta 必须在 [-8,20] 区间且为偶数，收到 {delta!r}")
    raw = delta if delta >= 0 else delta + (1 << 32)
    with setting_override("fontSizeDelta", raw, _reg_dword()):
        yield


@contextlib.contextmanager
def spacing_override(factor):
    """切换 view/lineSpacing（REG_SZ "1.5"）。仅允许菜单中的合法档位。"""
    if factor is None:
        yield
        return
    if factor not in SPACING_VALID:
        raise ValueError(f"lineSpacing 必须是 {SPACING_VALID} 之一，收到 {factor!r}")
    # Qt QSettings 把 double 写为去尾零的字符串："1.5" 而不是 "1.50"
    s = f"{factor:g}" if factor != int(factor) else f"{int(factor)}"
    with setting_override("lineSpacing", s, _reg_sz()):
        yield


@contextlib.contextmanager
def wrap_override(value):
    """切换 view/wordWrap（REG_SZ "true"/"false"）。"""
    if value is None:
        yield
        return
    if not isinstance(value, bool):
        raise ValueError(f"wordWrap 必须是 bool，收到 {value!r}")
    with setting_override("wordWrap", "true" if value else "false", _reg_sz()):
        yield


def _reg_sz():
    import winreg
    return winreg.REG_SZ


def _reg_dword():
    import winreg
    return winreg.REG_DWORD


def find_exe(cli_exe: str | None) -> Path:
    """按 CLI > 环境变量 > 自动搜索 顺序定位 exe。"""
    if cli_exe:
        p = Path(cli_exe).resolve()
        if not p.exists():
            sys.exit(f"[ERROR] --exe 指定的路径不存在: {p}")
        return p
    env = os.environ.get("SIMPLEMARKDOWN_EXE")
    if env:
        p = Path(env).resolve()
        if not p.exists():
            sys.exit(f"[ERROR] 环境变量 SIMPLEMARKDOWN_EXE 指向不存在的路径: {p}")
        return p
    for c in EXE_CANDIDATES:
        if c.exists():
            return c
    sys.exit(
        "[ERROR] 找不到 SimpleMarkdown 可执行文件。\n"
        f"  搜索过: {[str(c) for c in EXE_CANDIDATES]}\n"
        "  请先用 ENABLE_TEST_MODE 编译: cmake -S . -B build -DENABLE_TEST_MODE=ON && cmake --build build\n"
        "  或用 --exe 显式指定路径。"
    )


def kill_all_instances(exe_path: Path, quiet: bool = True) -> None:
    """杀掉所有 SimpleMarkdown 实例（清场，避免单实例参数转发）。"""
    name = exe_path.name
    if IS_WIN:
        cmd = ["taskkill", "/F", "/IM", name]
    else:
        cmd = ["pkill", "-f", name]
    subprocess.run(
        cmd,
        stdout=subprocess.DEVNULL if quiet else None,
        stderr=subprocess.DEVNULL if quiet else None,
        check=False,
    )


def wait_for_dump(start_ts: float, timeout: float) -> bool:
    """轮询直到 render_blocks.json 出现且 mtime > start_ts。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if RENDER_JSON.exists():
            mtime = RENDER_JSON.stat().st_mtime
            if mtime >= start_ts - 0.5:  # 0.5s 容差应对文件系统时间精度
                # 再等一小拍确保写完
                time.sleep(0.3)
                return True
        time.sleep(0.2)
    return False


def is_test_mode_build(exe_path: Path) -> bool:
    """启动 exe 一次（无参数），看会不会写出 render_blocks.json。"""
    if RENDER_JSON.exists():
        RENDER_JSON.unlink()
    kill_all_instances(exe_path)
    start = time.time()
    proc = subprocess.Popen([str(exe_path)], cwd=str(exe_path.parent))
    ok = wait_for_dump(start, timeout=8.0)
    kill_all_instances(exe_path)
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
    return ok


def run_sample(exe_path: Path, sample: Path, timeout: float, keep_json: bool) -> tuple[bool, str]:
    """跑一个样本，返回 (是否通过, 详情)。"""
    # 1. 清旧 JSON
    if RENDER_JSON.exists():
        RENDER_JSON.unlink()
    # 2. 清场
    kill_all_instances(exe_path)
    time.sleep(0.3)
    # 3. 启动
    start = time.time()
    proc = subprocess.Popen(
        [str(exe_path), str(sample.resolve())],
        cwd=str(exe_path.parent),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    # 4. 等渲染
    dumped = wait_for_dump(start, timeout=timeout)
    # 5. 杀进程
    kill_all_instances(exe_path)
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()

    if not dumped:
        return False, f"超时 {timeout}s 仍未生成 render_blocks.json（程序未渲染或测试模式未编入）"

    # 6. 可选保留 JSON
    if keep_json:
        tmp_dir = ROOT / "tmp"
        tmp_dir.mkdir(exist_ok=True)
        dst = tmp_dir / f"render_blocks_{sample.stem}.json"
        shutil.copy2(RENDER_JSON, dst)

    # 7. 校验
    result = subprocess.run(
        [sys.executable, str(VERIFY_PY), str(RENDER_JSON)],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    out = result.stdout + result.stderr
    return result.returncode == 0, out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", help="SimpleMarkdown 可执行文件路径")
    parser.add_argument("--sample", help="只跑一个样本（路径）")
    parser.add_argument("--timeout", type=float, default=15.0, help="单个样本等待渲染超时（秒）")
    parser.add_argument("--list", action="store_true", help="列出所有样本后退出")
    parser.add_argument("--keep-json", action="store_true", help="把每个样本的 JSON 复制到 tmp/")
    parser.add_argument("--verbose", "-v", action="store_true", help="打印 verify 的全部输出")
    parser.add_argument("--skip-probe", action="store_true", help="跳过启动前的测试模式探测")
    parser.add_argument("--no-build", action="store_true",
                        help="跳过编译，直接用现有 exe（仅迭代 verify 规则时用）")
    parser.add_argument("--clean-build", action="store_true",
                        help="清 CMakeCache 后全量重建（默认是增量）")
    parser.add_argument(
        "--theme",
        choices=["light", "dark", "both", "current"],
        default="current",
        help="主题：current(不切换) / light / dark / both",
    )
    parser.add_argument(
        "--zoom",
        default=None,
        help="字号增量 fontSizeDelta：'sweep' 用预设档位 (-4,0,4,10)；或逗号分隔自定义如 '0,4'",
    )
    parser.add_argument(
        "--spacing",
        default=None,
        help="行间距 lineSpacing：'sweep' 用 (1.0,1.5,2.0)；或逗号分隔档位如 '1.0,1.8'",
    )
    parser.add_argument(
        "--wrap",
        choices=["on", "off", "both", "sweep"],
        default=None,
        help="自动换行 wordWrap：on / off / both / sweep（=both）",
    )
    args = parser.parse_args()

    samples = sorted(TESTS_DIR.glob("*.md"))
    if args.sample:
        sel = Path(args.sample).resolve()
        if not sel.exists():
            sys.exit(f"[ERROR] 样本不存在: {sel}")
        samples = [sel]

    if args.list:
        for s in samples:
            print(s.relative_to(ROOT))
        return 0

    # Step 0: 默认先编译（保证测的是最新代码）
    if not args.no_build:
        try:
            build_with_test_mode(clean=args.clean_build)
        except RuntimeError as e:
            print(f"[ERROR] build failed: {e}")
            return 99
        # build 成功暗示 ENABLE_TEST_MODE 已编入，probe 冗余
        if not args.skip_probe:
            args.skip_probe = True
            print("[i] build 成功 → 自动跳过 probe\n")

    exe = find_exe(args.exe)
    print(f"[i] EXE   : {exe}")
    print(f"[i] DUMP  : {RENDER_JSON}")
    print(f"[i] 样本数: {len(samples)}")
    print()

    # 探测：当前 build 是否启用了 ENABLE_TEST_MODE
    if not args.skip_probe:
        print("[probe] 检测 build 是否启用 ENABLE_TEST_MODE ...")
        if not is_test_mode_build(exe):
            print("[probe] FAIL：启动后未生成 render_blocks.json")
            print("        当前 build 没有启用 ENABLE_TEST_MODE。请重新编译：")
            print("          cmake -S . -B build -DENABLE_TEST_MODE=ON")
            print("          cmake --build build --config Release")
            return 99
        print("[probe] OK：测试模式可正常 dump\n")

    # 解析 sweep 维度，每个维度是一个 [(value, label)] 列表
    try:
        themes = _parse_theme_sweep(args.theme)
        zooms = _parse_zoom_sweep(args.zoom)
        spacings = _parse_spacing_sweep(args.spacing)
        wraps = _parse_wrap_sweep(args.wrap)
    except ValueError as e:
        print(f"[ERROR] sweep 参数解析失败: {e}")
        return 99

    import itertools
    combos = list(itertools.product(themes, zooms, spacings, wraps))
    total_runs = len(samples) * len(combos)

    all_failures: list[tuple[str, str]] = []
    multi = len(combos) > 1

    for (t_val, t_lbl), (z_val, z_lbl), (s_val, s_lbl), (w_val, w_lbl) in combos:
        combo_label = "·".join(x for x in (t_lbl, z_lbl, s_lbl, w_lbl) if x)
        if not combo_label:
            combo_label = "default"
        if multi:
            print(f"\n========== 组合: {combo_label} ==========\n")
        try:
            with theme_override(t_val), zoom_override(z_val), spacing_override(s_val), wrap_override(w_val):
                kill_all_instances(exe, quiet=True)
                time.sleep(0.5)
                for i, sample in enumerate(samples, 1):
                    rel = sample.relative_to(ROOT)
                    print(f"[{combo_label} {i}/{len(samples)}] {rel}")
                    passed, detail = run_sample(exe, sample, args.timeout, args.keep_json)
                    if passed:
                        print("    PASS")
                        if args.verbose:
                            for line in detail.strip().splitlines()[-5:]:
                                print(f"      | {line}")
                    else:
                        print("    FAIL")
                        for line in detail.strip().splitlines():
                            print(f"      | {line}")
                        all_failures.append((combo_label, str(rel)))
                    print()
        except ValueError as e:
            print(f"[ERROR] {e}")
            return 99

    print("=" * 60)
    if not all_failures:
        print(f"  ALL PASSED ({total_runs}/{total_runs})")
        return 0
    print(f"  FAILED ({len(all_failures)}/{total_runs}):")
    for label, f in all_failures:
        print(f"    - [{label}] {f}")
    return min(len(all_failures), 99)


# ---- sweep 参数解析 ----

def _parse_theme_sweep(arg):
    if arg in (None, "current"):
        return [(None, "")]
    if arg == "both":
        return [("light", "light"), ("dark", "dark")]
    return [(arg, arg)]


def _parse_zoom_sweep(arg):
    if arg is None:
        return [(None, "")]
    if arg == "sweep":
        return [(z, f"z{z:+d}") for z in ZOOM_PRESET]
    parts = [int(x.strip()) for x in arg.split(",") if x.strip()]
    return [(z, f"z{z:+d}") for z in parts]


def _parse_spacing_sweep(arg):
    if arg is None:
        return [(None, "")]
    if arg == "sweep":
        return [(s, f"sp{s}") for s in SPACING_PRESET]
    parts = [float(x.strip()) for x in arg.split(",") if x.strip()]
    return [(s, f"sp{s}") for s in parts]


def _parse_wrap_sweep(arg):
    if arg is None:
        return [(None, "")]
    if arg in ("both", "sweep"):
        return [(True, "wrap-on"), (False, "wrap-off")]
    if arg == "on":
        return [(True, "wrap-on")]
    if arg == "off":
        return [(False, "wrap-off")]
    return [(None, "")]


if __name__ == "__main__":
    sys.exit(main())
