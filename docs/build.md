# SimpleMarkdown 构建说明

## 前置依赖

### Windows
- Visual Studio 2019 或更高版本（MSVC 编译器）
- Qt 5.15.x（msvc2019_64）
- CMake >= 3.16
- Git（用于 submodule）

### Linux
- GCC 9+ 或 Clang 10+
- Qt 5.15 开发包（`sudo apt install qtbase5-dev`）
- CMake >= 3.16
- Git

## 获取源码

```bash
git clone --recursive <repo-url>
cd easy_markdown
```

如果已 clone 但未初始化 submodule：
```bash
git submodule update --init --recursive
```

## 编译

### Windows (Visual Studio)
```bash
cmake -S . -B build
cmake --build build --config Release
```

### Windows (Ninja)
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Linux
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## 运行测试

```bash
cd build
ctest -C Release --output-on-failure
```

## 运行

```bash
# Windows
build/app/Release/SimpleMarkdown.exe

# Linux
build/app/SimpleMarkdown

# 打开文件
build/app/Release/SimpleMarkdown.exe path/to/file.md
```
