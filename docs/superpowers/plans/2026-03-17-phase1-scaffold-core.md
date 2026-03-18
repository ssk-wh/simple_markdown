# SimpleMarkdown Phase 1: 项目脚手架 + 核心数据模型

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 搭建项目构建系统，实现核心数据模型（PieceTable、UndoStack、Selection、Document），通过全部单元测试。

**Architecture:** 纯数据层，无 GUI 组件。PieceTable 作为文本存储引擎，UndoStack 记录编辑操作支持撤销重做，Selection 管理选区，Document 整合三者并提供文件 I/O 和 Qt 信号。

**Tech Stack:** C++17, Qt 5.15 (Core only), CMake >= 3.16, cmark-gfm (submodule, 仅编译验证), Google Test

**Spec:** `docs/superpowers/specs/2026-03-17-easy-markdown-design.md`

---

## 文件结构

```
easy_markdown/
├── CMakeLists.txt                          # 顶层 CMake
├── .gitignore
├── .claudeignore
├── .gitmodules                             # submodule 配置
├── 3rdparty/
│   ├── cmark-gfm/                          # git submodule
│   └── googletest/                         # git submodule
├── src/
│   ├── CMakeLists.txt
│   └── core/
│       ├── CMakeLists.txt
│       ├── MappedFile.h / .cpp             # mmap 跨平台封装
│       ├── PieceTable.h / .cpp             # Piece Table 文本存储
│       ├── LineIndex.h / .cpp              # 行偏移索引（骨架）
│       ├── UndoStack.h / .cpp              # 撤销/重做
│       ├── Selection.h / .cpp              # 选区模型
│       └── Document.h / .cpp               # 文档模型整合
├── tests/
│   ├── CMakeLists.txt
│   ├── test_MappedFile.cpp
│   ├── test_PieceTable.cpp
│   ├── test_UndoStack.cpp
│   ├── test_Selection.cpp
│   └── test_Document.cpp
└── docs/                                   # 已有
```

---

### Task 1: 项目脚手架

**Files:**
- Create: `CMakeLists.txt`, `src/CMakeLists.txt`, `src/core/CMakeLists.txt`, `tests/CMakeLists.txt`
- Create: `.gitignore`, `.claudeignore`
- Create: 所有 `src/core/*.h` 和 `src/core/*.cpp` 的空桩文件
- Create: 所有 `tests/test_*.cpp` 的空桩文件（含一个占位 TEST）

- [ ] **Step 1: 创建 .gitignore**

```gitignore
# Build
build/
out/
cmake-build-*/

# IDE
.vs/
.idea/
.vscode/
*.user
*.suo

# OS
Thumbs.db
.DS_Store

# Compiled
*.obj
*.o
*.exe
*.dll
*.so
*.dylib
*.pdb
*.ilk

# Stackdump
*.stackdump
```

- [ ] **Step 2: 创建 .claudeignore**

```
build/
out/
3rdparty/cmark-gfm/
3rdparty/googletest/
.git/
.vs/
*.obj
*.pdb
*.exe
*.dll
*.stackdump
```

- [ ] **Step 3: 创建顶层 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)
project(SimpleMarkdown VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt 5.15
find_package(Qt5 REQUIRED COMPONENTS Core)

# cmark-gfm
option(CMARK_TESTS "" OFF)
option(CMARK_SHARED "" OFF)
add_subdirectory(3rdparty/cmark-gfm)

# GoogleTest
option(BUILD_GMOCK "" OFF)
option(INSTALL_GTEST "" OFF)
add_subdirectory(3rdparty/googletest)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 4: 创建 src/CMakeLists.txt**

```cmake
add_subdirectory(core)
```

- [ ] **Step 5: 创建 src/core/CMakeLists.txt**

```cmake
add_library(easy_core STATIC
    MappedFile.h MappedFile.cpp
    PieceTable.h PieceTable.cpp
    LineIndex.h LineIndex.cpp
    UndoStack.h UndoStack.cpp
    Selection.h Selection.cpp
    Document.h Document.cpp
)

target_include_directories(easy_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(easy_core PUBLIC Qt5::Core)
```

- [ ] **Step 6: 创建 tests/CMakeLists.txt**

```cmake
find_package(Qt5 REQUIRED COMPONENTS Core)

function(add_unit_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE easy_core gtest gtest_main)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

add_unit_test(test_MappedFile test_MappedFile.cpp)
add_unit_test(test_PieceTable test_PieceTable.cpp)
add_unit_test(test_UndoStack test_UndoStack.cpp)
add_unit_test(test_Selection test_Selection.cpp)
add_unit_test(test_Document  test_Document.cpp)
```

- [ ] **Step 7: 创建所有空桩源文件和测试文件**

每个 `.h` 文件放 include guard + 空类声明，每个 `.cpp` include 对应头文件。
每个 `test_*.cpp` 放一个 `#include <gtest/gtest.h>` + 一个占位 `TEST(Placeholder, Compiles) { SUCCEED(); }`。

- [ ] **Step 8: 验证编译**

Run: `cmake -S . -B build -G Ninja && cmake --build build && cd build && ctest --output-on-failure`
Expected: 全部 5 个测试 PASS

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "chore: 搭建CMake构建系统和项目目录结构"
```

---

### Task 2: 添加 git submodule

**Files:**
- Create: `.gitmodules`
- Create: `3rdparty/cmark-gfm/` (submodule)
- Create: `3rdparty/googletest/` (submodule)

- [ ] **Step 1: 添加 submodule**

```bash
git submodule add https://github.com/github/cmark-gfm.git 3rdparty/cmark-gfm
git submodule add https://github.com/google/googletest.git 3rdparty/googletest
cd 3rdparty/googletest && git checkout v1.14.0 && cd ../..
cd 3rdparty/cmark-gfm && git checkout 0.29.0.gfm.13 && cd ../..
```

- [ ] **Step 2: 验证编译**

Run: `cmake -S . -B build -G Ninja && cmake --build build`
Expected: cmark-gfm 和 googletest 编译通过，easy_core 空桩库链接成功

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "chore: 添加cmark-gfm和googletest作为git submodule"
```

---

### Task 3: MappedFile — mmap 跨平台封装

**Files:**
- Create: `src/core/MappedFile.h`
- Create: `src/core/MappedFile.cpp`
- Test: `tests/test_MappedFile.cpp`

- [ ] **Step 1: 写测试**

```cpp
#include <gtest/gtest.h>
#include "MappedFile.h"
#include <QFile>
#include <QTemporaryFile>

TEST(MappedFile, OpenNonExistentFileFails) {
    MappedFile mf;
    EXPECT_FALSE(mf.open("__nonexistent_file_12345.txt"));
    EXPECT_FALSE(mf.isOpen());
}

TEST(MappedFile, OpenAndReadFile) {
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    ASSERT_TRUE(tmp.open());
    tmp.write("Hello, MappedFile!");
    tmp.flush();
    QString path = tmp.fileName();
    tmp.close();

    MappedFile mf;
    ASSERT_TRUE(mf.open(path));
    EXPECT_TRUE(mf.isOpen());
    EXPECT_EQ(mf.size(), 18u);
    EXPECT_EQ(std::string(mf.data(), mf.size()), "Hello, MappedFile!");
    EXPECT_EQ(mf.toQString(), "Hello, MappedFile!");
}

TEST(MappedFile, EmptyFile) {
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    ASSERT_TRUE(tmp.open());
    QString path = tmp.fileName();
    tmp.close();

    MappedFile mf;
    ASSERT_TRUE(mf.open(path));
    EXPECT_TRUE(mf.isOpen());
    EXPECT_EQ(mf.size(), 0u);
}

TEST(MappedFile, MoveSemantics) {
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    ASSERT_TRUE(tmp.open());
    tmp.write("move test");
    tmp.flush();
    QString path = tmp.fileName();
    tmp.close();

    MappedFile mf1;
    ASSERT_TRUE(mf1.open(path));
    MappedFile mf2(std::move(mf1));
    EXPECT_FALSE(mf1.isOpen());
    EXPECT_TRUE(mf2.isOpen());
    EXPECT_EQ(mf2.toQString(), "move test");
}

TEST(MappedFile, CloseReleasesResources) {
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    ASSERT_TRUE(tmp.open());
    tmp.write("close test");
    tmp.flush();
    QString path = tmp.fileName();
    tmp.close();

    MappedFile mf;
    ASSERT_TRUE(mf.open(path));
    mf.close();
    EXPECT_FALSE(mf.isOpen());
    EXPECT_EQ(mf.data(), nullptr);
    EXPECT_EQ(mf.size(), 0u);
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cmake --build build && cd build && ctest -R test_MappedFile --output-on-failure`
Expected: FAIL（空桩实现）

- [ ] **Step 3: 实现 MappedFile**

`MappedFile.h` 接口：
```cpp
#pragma once
#include <QString>
#include <cstddef>

class MappedFile {
public:
    MappedFile();
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    bool open(const QString& filePath);
    void close();
    bool isOpen() const;
    const char* data() const;
    size_t size() const;
    QString toQString() const;

private:
    void* m_data = nullptr;
    size_t m_size = 0;
#ifdef _WIN32
    void* m_fileHandle = nullptr;
    void* m_mappingHandle = nullptr;
#else
    int m_fd = -1;
#endif
};
```

`MappedFile.cpp`：
- Windows: `CreateFileW` → `CreateFileMappingW` → `MapViewOfFile`（只读）
- Linux: `open()` → `fstat()` → `mmap(PROT_READ, MAP_PRIVATE)`
- 空文件：`size()==0` 时 `data()` 返回 nullptr，`isOpen()` 返回 true
- Move 语义正确转移资源

- [ ] **Step 4: 运行测试确认通过**

Run: `cmake --build build && cd build && ctest -R test_MappedFile --output-on-failure`
Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add src/core/MappedFile.h src/core/MappedFile.cpp tests/test_MappedFile.cpp
git commit -m "feat: 实现MappedFile跨平台内存映射封装"
```

---

### Task 4: PieceTable — 核心文本存储引擎

**Files:**
- Create: `src/core/PieceTable.h`
- Create: `src/core/PieceTable.cpp`
- Test: `tests/test_PieceTable.cpp`

- [ ] **Step 1: 写测试**

```cpp
#include <gtest/gtest.h>
#include "PieceTable.h"

// === 基础构造 ===
TEST(PieceTable, EmptyConstruction) {
    PieceTable pt;
    EXPECT_EQ(pt.length(), 0);
    EXPECT_TRUE(pt.isEmpty());
    EXPECT_EQ(pt.text(), "");
    EXPECT_EQ(pt.lineCount(), 1);
}

TEST(PieceTable, ConstructWithText) {
    PieceTable pt("Hello");
    EXPECT_EQ(pt.length(), 5);
    EXPECT_EQ(pt.text(), "Hello");
}

// === 插入 ===
TEST(PieceTable, InsertAtBeginning) {
    PieceTable pt("World");
    pt.insert(0, "Hello ");
    EXPECT_EQ(pt.text(), "Hello World");
}

TEST(PieceTable, InsertAtEnd) {
    PieceTable pt("Hello");
    pt.insert(5, " World");
    EXPECT_EQ(pt.text(), "Hello World");
}

TEST(PieceTable, InsertInMiddle) {
    PieceTable pt("HellWorld");
    pt.insert(4, "o ");
    EXPECT_EQ(pt.text(), "Hello World");
}

TEST(PieceTable, InsertIntoEmpty) {
    PieceTable pt;
    pt.insert(0, "Hello");
    EXPECT_EQ(pt.text(), "Hello");
}

TEST(PieceTable, MultipleInserts) {
    PieceTable pt;
    pt.insert(0, "A");
    pt.insert(1, "C");
    pt.insert(1, "B");
    EXPECT_EQ(pt.text(), "ABC");
}

// === 删除 ===
TEST(PieceTable, RemoveFromBeginning) {
    PieceTable pt("Hello World");
    pt.remove(0, 6);
    EXPECT_EQ(pt.text(), "World");
}

TEST(PieceTable, RemoveFromEnd) {
    PieceTable pt("Hello World");
    pt.remove(5, 6);
    EXPECT_EQ(pt.text(), "Hello");
}

TEST(PieceTable, RemoveFromMiddle) {
    PieceTable pt("Hello World");
    pt.remove(5, 1);
    EXPECT_EQ(pt.text(), "HelloWorld");
}

TEST(PieceTable, RemoveAll) {
    PieceTable pt("Hello");
    pt.remove(0, 5);
    EXPECT_EQ(pt.text(), "");
    EXPECT_TRUE(pt.isEmpty());
}

TEST(PieceTable, RemoveAcrossPieces) {
    PieceTable pt("ABCDE");
    pt.insert(2, "XY");
    pt.remove(1, 4);
    EXPECT_EQ(pt.text(), "ADE");
}

// === 替换 ===
TEST(PieceTable, ReplaceText) {
    PieceTable pt("Hello World");
    pt.replace(6, 5, "Earth");
    EXPECT_EQ(pt.text(), "Hello Earth");
}

TEST(PieceTable, ReplaceWithDifferentLength) {
    PieceTable pt("abc");
    pt.replace(1, 1, "XYZ");
    EXPECT_EQ(pt.text(), "aXYZc");
}

// === textAt ===
TEST(PieceTable, TextAtSubstring) {
    PieceTable pt("Hello World");
    EXPECT_EQ(pt.textAt(6, 5), "World");
    EXPECT_EQ(pt.textAt(0, 5), "Hello");
}

// === 行操作 ===
TEST(PieceTable, LineCountNoNewlines) {
    PieceTable pt("Hello");
    EXPECT_EQ(pt.lineCount(), 1);
}

TEST(PieceTable, LineCountWithNewlines) {
    PieceTable pt("Line1\nLine2\nLine3");
    EXPECT_EQ(pt.lineCount(), 3);
}

TEST(PieceTable, LineCountTrailingNewline) {
    PieceTable pt("Line1\nLine2\n");
    EXPECT_EQ(pt.lineCount(), 3);
}

TEST(PieceTable, LineText) {
    PieceTable pt("Line1\nLine2\nLine3");
    EXPECT_EQ(pt.lineText(0), "Line1");
    EXPECT_EQ(pt.lineText(1), "Line2");
    EXPECT_EQ(pt.lineText(2), "Line3");
}

TEST(PieceTable, LineTextAfterInsert) {
    PieceTable pt("AAA\nBBB");
    pt.insert(4, "CCC\n");
    EXPECT_EQ(pt.lineCount(), 3);
    EXPECT_EQ(pt.lineText(0), "AAA");
    EXPECT_EQ(pt.lineText(1), "CCC");
    EXPECT_EQ(pt.lineText(2), "BBB");
}

TEST(PieceTable, OffsetToLine) {
    PieceTable pt("AB\nCD\nEF");
    EXPECT_EQ(pt.offsetToLine(0), 0);
    EXPECT_EQ(pt.offsetToLine(1), 0);
    EXPECT_EQ(pt.offsetToLine(2), 0);
    EXPECT_EQ(pt.offsetToLine(3), 1);
    EXPECT_EQ(pt.offsetToLine(6), 2);
}

TEST(PieceTable, LineToOffset) {
    PieceTable pt("AB\nCD\nEF");
    EXPECT_EQ(pt.lineToOffset(0), 0);
    EXPECT_EQ(pt.lineToOffset(1), 3);
    EXPECT_EQ(pt.lineToOffset(2), 6);
}

TEST(PieceTable, EmptyLineText) {
    PieceTable pt("\n\n");
    EXPECT_EQ(pt.lineCount(), 3);
    EXPECT_EQ(pt.lineText(0), "");
    EXPECT_EQ(pt.lineText(1), "");
    EXPECT_EQ(pt.lineText(2), "");
}

TEST(PieceTable, ChineseText) {
    PieceTable pt(QString::fromUtf8("\u4f60\u597d\u4e16\u754c"));
    EXPECT_EQ(pt.length(), 4);
    pt.insert(2, QString::fromUtf8("\uff0c"));
    EXPECT_EQ(pt.text(), QString::fromUtf8("\u4f60\u597d\uff0c\u4e16\u754c"));
    EXPECT_EQ(pt.length(), 5);
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cmake --build build && cd build && ctest -R test_PieceTable --output-on-failure`
Expected: FAIL

- [ ] **Step 3: 实现 PieceTable**

`PieceTable.h` 接口：
```cpp
#pragma once
#include <QString>
#include <vector>
#include <cstdint>

struct Piece {
    enum Source : uint8_t { Original, Add };
    Source source;
    uint32_t start;
    uint32_t length;
    uint32_t lineFeeds;
};

class PieceTable {
public:
    PieceTable();
    explicit PieceTable(const QString& text);

    void insert(int offset, const QString& text);
    void remove(int offset, int length);
    void replace(int offset, int length, const QString& text);

    QString text() const;
    QString textAt(int offset, int length) const;
    int length() const;
    bool isEmpty() const;

    int lineCount() const;
    QString lineText(int line) const;
    int offsetToLine(int offset) const;
    int lineToOffset(int line) const;

private:
    QString m_original;
    QString m_add;
    std::vector<Piece> m_pieces;
    std::vector<uint32_t> m_lineFeedPrefix;

    const QString& bufferFor(Piece::Source source) const;
    int findPieceAtOffset(int offset, int& offsetInPiece) const;
    static uint32_t countLineFeeds(const QString& buf, int start, int length);
    void updateLineFeedPrefix();
};
```

实现要点：
- `insert(offset, text)`：找到 offset 所在 piece → 分裂为 [前|新|后] → 新 piece source=Add, start=m_add.size()，追加 text 到 m_add → updateLineFeedPrefix
- `remove(offset, length)`：找到起始和结束 piece → 截断/删除受影响 pieces → updateLineFeedPrefix
- `replace`：remove + insert
- `lineCount`：pieces 为空时 1，否则 1 + m_lineFeedPrefix.back()
- `offsetToLine`：遍历 pieces 累加字符数定位 offset，然后累加 lineFeeds
- `lineToOffset`：遍历 pieces，累加 lineFeeds 直到达到目标行
- `updateLineFeedPrefix`：全量重建前缀和数组

- [ ] **Step 4: 运行测试确认通过**

Run: `cmake --build build && cd build && ctest -R test_PieceTable --output-on-failure`
Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add src/core/PieceTable.h src/core/PieceTable.cpp tests/test_PieceTable.cpp
git commit -m "feat: 实现PieceTable文本存储引擎"
```

---

### Task 5: LineIndex 骨架

**Files:**
- Create: `src/core/LineIndex.h`
- Create: `src/core/LineIndex.cpp`

- [ ] **Step 1: 创建 LineIndex 头文件（接口定义）**

```cpp
#pragma once
#include <QString>
#include <vector>

class LineIndex {
public:
    LineIndex();
    void build(const QChar* data, int length);
    int lineCount() const;
    int offsetToLine(int offset) const;
    int lineToOffset(int line) const;
    int lineLength(int line) const;

private:
    std::vector<int> m_lineStarts;
    int m_totalLength = 0;
};
```

- [ ] **Step 2: 创建空实现桩**

```cpp
#include "LineIndex.h"

LineIndex::LineIndex() = default;
void LineIndex::build(const QChar*, int) {}
int LineIndex::lineCount() const { return 1; }
int LineIndex::offsetToLine(int) const { return 0; }
int LineIndex::lineToOffset(int) const { return 0; }
int LineIndex::lineLength(int) const { return 0; }
```

- [ ] **Step 3: 验证编译通过**

Run: `cmake --build build`
Expected: 编译通过

- [ ] **Step 4: Commit**

```bash
git add src/core/LineIndex.h src/core/LineIndex.cpp
git commit -m "feat: 添加LineIndex行索引骨架"
```

---

### Task 6: UndoStack — 撤销/重做系统

**Files:**
- Create: `src/core/UndoStack.h`
- Create: `src/core/UndoStack.cpp`
- Test: `tests/test_UndoStack.cpp`

- [ ] **Step 1: 写测试**

```cpp
#include <gtest/gtest.h>
#include "UndoStack.h"

TEST(UndoStack, InitialState) {
    UndoStack us;
    EXPECT_FALSE(us.canUndo());
    EXPECT_FALSE(us.canRedo());
    EXPECT_TRUE(us.isAtSavePoint());
}

TEST(UndoStack, PushAndUndo) {
    UndoStack us;
    us.push(0, "", "A");
    EXPECT_TRUE(us.canUndo());
    auto op = us.undo();
    EXPECT_EQ(op.offset, 0);
    EXPECT_EQ(op.addedText, "A");
    EXPECT_EQ(op.removedText, "");
    EXPECT_FALSE(us.canUndo());
    EXPECT_TRUE(us.canRedo());
}

TEST(UndoStack, UndoRedo) {
    UndoStack us;
    us.push(0, "", "Hello");
    us.undo();
    auto op = us.redo();
    EXPECT_EQ(op.addedText, "Hello");
    EXPECT_TRUE(us.canUndo());
    EXPECT_FALSE(us.canRedo());
}

TEST(UndoStack, NewPushClearsRedoStack) {
    UndoStack us;
    us.push(0, "", "A");
    us.undo();
    us.push(0, "", "B");
    EXPECT_FALSE(us.canRedo());
}

TEST(UndoStack, MergeContinuousTyping) {
    UndoStack us;
    us.setMergeInterval(10000);
    us.push(0, "", "A");
    us.push(1, "", "B");
    us.push(2, "", "C");
    auto op = us.undo();
    EXPECT_EQ(op.addedText, "ABC");
    EXPECT_FALSE(us.canUndo());
}

TEST(UndoStack, NoMergeOnSpace) {
    UndoStack us;
    us.setMergeInterval(10000);
    us.push(0, "", "A");
    us.push(1, "", " ");
    auto op = us.undo();
    EXPECT_EQ(op.addedText, " ");
    EXPECT_TRUE(us.canUndo());
}

TEST(UndoStack, NoMergeOnNewline) {
    UndoStack us;
    us.setMergeInterval(10000);
    us.push(0, "", "A");
    us.push(1, "", "\n");
    auto op = us.undo();
    EXPECT_EQ(op.addedText, "\n");
    EXPECT_TRUE(us.canUndo());
}

TEST(UndoStack, MergeContinuousBackspace) {
    UndoStack us;
    us.setMergeInterval(10000);
    us.push(3, "D", "");
    us.push(2, "C", "");
    us.push(1, "B", "");
    auto op = us.undo();
    EXPECT_EQ(op.removedText, "BCD");
    EXPECT_EQ(op.offset, 1);
    EXPECT_FALSE(us.canUndo());
}

TEST(UndoStack, NoMergeNonConsecutiveOffset) {
    UndoStack us;
    us.setMergeInterval(10000);
    us.push(0, "", "A");
    us.push(5, "", "B");
    auto op = us.undo();
    EXPECT_EQ(op.addedText, "B");
    EXPECT_TRUE(us.canUndo());
}

TEST(UndoStack, SavePoint) {
    UndoStack us;
    us.push(0, "", "A");
    EXPECT_FALSE(us.isAtSavePoint());
    us.setSavePoint();
    EXPECT_TRUE(us.isAtSavePoint());
    us.push(1, "", "B");
    EXPECT_FALSE(us.isAtSavePoint());
    us.undo();
    EXPECT_TRUE(us.isAtSavePoint());
}

TEST(UndoStack, SavePointAfterUndoPastSave) {
    UndoStack us;
    us.push(0, "", "A");
    us.setSavePoint();
    us.undo();
    EXPECT_FALSE(us.isAtSavePoint());
    us.redo();
    EXPECT_TRUE(us.isAtSavePoint());
}

TEST(UndoStack, Clear) {
    UndoStack us;
    us.push(0, "", "A");
    us.clear();
    EXPECT_FALSE(us.canUndo());
    EXPECT_FALSE(us.canRedo());
    EXPECT_TRUE(us.isAtSavePoint());
}

TEST(UndoStack, MultipleUndoRedo) {
    UndoStack us;
    us.setMergeInterval(0);
    us.push(0, "", "A");
    us.push(1, "", "B");
    us.push(2, "", "C");
    auto op1 = us.undo(); EXPECT_EQ(op1.addedText, "C");
    auto op2 = us.undo(); EXPECT_EQ(op2.addedText, "B");
    auto op3 = us.redo(); EXPECT_EQ(op3.addedText, "B");
    auto op4 = us.redo(); EXPECT_EQ(op4.addedText, "C");
}

TEST(UndoStack, DeleteOperation) {
    UndoStack us;
    us.push(5, "World", "");
    auto op = us.undo();
    EXPECT_EQ(op.offset, 5);
    EXPECT_EQ(op.removedText, "World");
}

TEST(UndoStack, ReplaceOperation) {
    UndoStack us;
    us.push(0, "old", "new");
    auto op = us.undo();
    EXPECT_EQ(op.removedText, "old");
    EXPECT_EQ(op.addedText, "new");
}
```

- [ ] **Step 2: 运行测试确认失败**

Run: `cmake --build build && cd build && ctest -R test_UndoStack --output-on-failure`
Expected: FAIL

- [ ] **Step 3: 实现 UndoStack**

`UndoStack.h` 接口：
```cpp
#pragma once
#include <QString>
#include <vector>
#include <cstdint>

struct EditOperation {
    int offset;
    QString removedText;
    QString addedText;
    int64_t timestamp;
};

class UndoStack {
public:
    UndoStack();
    void push(int offset, const QString& removedText, const QString& addedText);
    bool canUndo() const;
    EditOperation undo();
    bool canRedo() const;
    EditOperation redo();
    void setSavePoint();
    bool isAtSavePoint() const;
    void clear();
    void setMergeInterval(int64_t ms);

private:
    std::vector<EditOperation> m_undoStack;
    std::vector<EditOperation> m_redoStack;
    int m_savePoint = 0;
    int64_t m_mergeInterval = 300;
    bool shouldMerge(const EditOperation& existing, int offset,
                     const QString& removedText, const QString& addedText) const;
    int64_t currentTimestamp() const;
};
```

实现要点：
- `push`：清空 redoStack → 检查合并条件 → 合并或新建
- 合并条件（插入）：两者都是纯插入，间隔 < mergeInterval，新 offset == old.offset + old.addedText.length()，addedText 是单字符且非空格/换行
- 合并条件（退格）：两者都是纯删除，间隔 < mergeInterval，新 offset == old.offset - 1
- `savePoint`：记录 m_undoStack.size()

- [ ] **Step 4: 运行测试确认通过**

Run: `cmake --build build && cd build && ctest -R test_UndoStack --output-on-failure`
Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add src/core/UndoStack.h src/core/UndoStack.cpp tests/test_UndoStack.cpp
git commit -m "feat: 实现UndoStack撤销重做系统"
```

---

### Task 7: Selection — 选区模型

**Files:**
- Create: `src/core/Selection.h`
- Create: `src/core/Selection.cpp`
- Test: `tests/test_Selection.cpp`

- [ ] **Step 1: 写测试**

```cpp
#include <gtest/gtest.h>
#include "Selection.h"

TEST(TextPosition, Comparison) {
    TextPosition a{0, 5}, b{1, 0}, c{0, 5};
    EXPECT_TRUE(a < b);
    EXPECT_TRUE(a == c);
    EXPECT_TRUE(b > a);
}

TEST(SelectionRange, Empty) {
    SelectionRange r{{1, 2}, {1, 2}};
    EXPECT_TRUE(r.isEmpty());
}

TEST(SelectionRange, Forward) {
    SelectionRange r{{0, 0}, {1, 5}};
    EXPECT_TRUE(r.isForward());
    EXPECT_EQ(r.start(), (TextPosition{0, 0}));
    EXPECT_EQ(r.end(), (TextPosition{1, 5}));
}

TEST(SelectionRange, Backward) {
    SelectionRange r{{1, 5}, {0, 0}};
    EXPECT_FALSE(r.isForward());
    EXPECT_EQ(r.start(), (TextPosition{0, 0}));
    EXPECT_EQ(r.end(), (TextPosition{1, 5}));
}

TEST(Selection, InitialState) {
    Selection sel;
    EXPECT_EQ(sel.cursorPosition(), (TextPosition{0, 0}));
    EXPECT_FALSE(sel.hasSelection());
}

TEST(Selection, SetCursorClearsSelection) {
    Selection sel;
    sel.setSelection({0, 0}, {1, 5});
    sel.setCursorPosition({2, 0});
    EXPECT_FALSE(sel.hasSelection());
    EXPECT_EQ(sel.cursorPosition(), (TextPosition{2, 0}));
}

TEST(Selection, ExtendSelection) {
    Selection sel;
    sel.setCursorPosition({1, 0});
    sel.extendSelection({2, 5});
    EXPECT_TRUE(sel.hasSelection());
    EXPECT_EQ(sel.range().anchor, (TextPosition{1, 0}));
    EXPECT_EQ(sel.range().cursor, (TextPosition{2, 5}));
}

TEST(Selection, ClearSelection) {
    Selection sel;
    sel.setSelection({0, 0}, {3, 0});
    sel.clearSelection();
    EXPECT_FALSE(sel.hasSelection());
    EXPECT_EQ(sel.cursorPosition(), (TextPosition{3, 0}));
}

TEST(Selection, PreferredColumn) {
    Selection sel;
    EXPECT_EQ(sel.preferredColumn(), -1);
    sel.setPreferredColumn(10);
    EXPECT_EQ(sel.preferredColumn(), 10);
    sel.resetPreferredColumn();
    EXPECT_EQ(sel.preferredColumn(), -1);
}
```

- [ ] **Step 2: 运行测试确认失败**
- [ ] **Step 3: 实现 Selection**

纯数据结构，TextPosition 比较运算符 + SelectionRange start/end/isEmpty/isForward + Selection 状态管理。

- [ ] **Step 4: 运行测试确认通过**

Run: `cmake --build build && cd build && ctest -R test_Selection --output-on-failure`
Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add src/core/Selection.h src/core/Selection.cpp tests/test_Selection.cpp
git commit -m "feat: 实现Selection选区模型"
```

---

### Task 8: Document — 文档模型

**Files:**
- Create: `src/core/Document.h`
- Create: `src/core/Document.cpp`
- Test: `tests/test_Document.cpp`

- [ ] **Step 1: 写测试**

```cpp
#include <gtest/gtest.h>
#include "Document.h"
#include <QTemporaryFile>
#include <QFile>
#include <QSignalSpy>

TEST(Document, InitialState) {
    Document doc;
    EXPECT_TRUE(doc.isEmpty());
    EXPECT_EQ(doc.length(), 0);
    EXPECT_FALSE(doc.isModified());
    EXPECT_EQ(doc.lineCount(), 1);
}

TEST(Document, InsertEmitsSignal) {
    Document doc;
    QSignalSpy spy(&doc, &Document::textChanged);
    doc.insert(0, "Hello");
    ASSERT_EQ(spy.count(), 1);
    auto args = spy.takeFirst();
    EXPECT_EQ(args.at(0).toInt(), 0);
    EXPECT_EQ(args.at(1).toInt(), 0);
    EXPECT_EQ(args.at(2).toInt(), 5);
    EXPECT_EQ(doc.text(), "Hello");
}

TEST(Document, InsertMarksModified) {
    Document doc;
    doc.insert(0, "A");
    EXPECT_TRUE(doc.isModified());
}

TEST(Document, UndoRedo) {
    Document doc;
    doc.insert(0, "Hello");
    doc.insert(5, " World");
    EXPECT_EQ(doc.text(), "Hello World");
    doc.undo();
    EXPECT_EQ(doc.text(), "Hello");
    doc.undo();
    EXPECT_EQ(doc.text(), "");
    doc.redo();
    EXPECT_EQ(doc.text(), "Hello");
    doc.redo();
    EXPECT_EQ(doc.text(), "Hello World");
}

TEST(Document, UndoRestoresModifiedState) {
    Document doc;
    doc.insert(0, "A");
    EXPECT_TRUE(doc.isModified());
    doc.undo();
    EXPECT_FALSE(doc.isModified());
}

TEST(Document, RemoveAndUndo) {
    Document doc;
    doc.insert(0, "ABCDE");
    doc.remove(1, 3);
    EXPECT_EQ(doc.text(), "AE");
    doc.undo();
    EXPECT_EQ(doc.text(), "ABCDE");
}

TEST(Document, Replace) {
    Document doc;
    doc.insert(0, "Hello World");
    doc.replace(6, 5, "Earth");
    EXPECT_EQ(doc.text(), "Hello Earth");
}

TEST(Document, LoadAndSaveFile) {
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    ASSERT_TRUE(tmp.open());
    tmp.write("Line1\nLine2\nLine3");
    tmp.flush();
    QString path = tmp.fileName();
    tmp.close();

    Document doc;
    ASSERT_TRUE(doc.loadFromFile(path));
    EXPECT_EQ(doc.text(), "Line1\nLine2\nLine3");
    EXPECT_EQ(doc.lineCount(), 3);
    EXPECT_FALSE(doc.isModified());

    doc.insert(5, "A");
    EXPECT_TRUE(doc.isModified());

    QTemporaryFile out;
    out.setAutoRemove(true);
    ASSERT_TRUE(out.open());
    QString outPath = out.fileName();
    out.close();

    ASSERT_TRUE(doc.saveToFile(outPath));
    EXPECT_FALSE(doc.isModified());

    QFile f(outPath);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_EQ(f.readAll(), QByteArray("Line1A\nLine2\nLine3"));
}

TEST(Document, CRLFHandling) {
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    ASSERT_TRUE(tmp.open());
    tmp.write("Line1\r\nLine2\r\nLine3");
    tmp.flush();
    QString path = tmp.fileName();
    tmp.close();

    Document doc;
    ASSERT_TRUE(doc.loadFromFile(path));
    EXPECT_EQ(doc.text(), "Line1\nLine2\nLine3");
    EXPECT_EQ(doc.detectedLineEnding(), Document::CRLF);

    QTemporaryFile out;
    out.setAutoRemove(true);
    ASSERT_TRUE(out.open());
    QString outPath = out.fileName();
    out.close();

    ASSERT_TRUE(doc.saveToFile(outPath));
    QFile f(outPath);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_EQ(f.readAll(), QByteArray("Line1\r\nLine2\r\nLine3"));
}

TEST(Document, LineQueries) {
    Document doc;
    doc.insert(0, "First\nSecond\nThird");
    EXPECT_EQ(doc.lineCount(), 3);
    EXPECT_EQ(doc.lineText(0), "First");
    EXPECT_EQ(doc.lineText(1), "Second");
    EXPECT_EQ(doc.lineText(2), "Third");
}

TEST(Document, SelectionIntegration) {
    Document doc;
    doc.insert(0, "Hello World");
    doc.selection().setSelection({0, 0}, {0, 5});
    EXPECT_TRUE(doc.selection().hasSelection());
}

TEST(Document, LoadNonExistentFile) {
    Document doc;
    EXPECT_FALSE(doc.loadFromFile("__nonexistent__12345.txt"));
}
```

- [ ] **Step 2: 运行测试确认失败**
- [ ] **Step 3: 实现 Document**

`Document.h`：继承 QObject，整合 PieceTable + UndoStack + Selection。
关键点：
- `insert/remove/replace`：操作 PieceTable → push UndoStack → emit textChanged
- `undo`：从 UndoStack 取操作 → **直接**操作 PieceTable（不经过 push）→ emit textChanged
- `loadFromFile`：QFile::readAll → normalizeLineEndings → 新建 PieceTable → 清空 UndoStack → setSavePoint
- `saveToFile`：text() → denormalizeLineEndings → QFile::write → setSavePoint

- [ ] **Step 4: 运行测试确认通过**

Run: `cmake --build build && cd build && ctest -R test_Document --output-on-failure`
Expected: ALL PASS

- [ ] **Step 5: Commit**

```bash
git add src/core/Document.h src/core/Document.cpp tests/test_Document.cpp
git commit -m "feat: 实现Document文档模型"
```

---

### Task 9: 全量测试 + 边界补充

**Files:**
- Modify: `tests/test_PieceTable.cpp`（追加性能测试）

- [ ] **Step 1: 运行全量测试**

Run: `cmake --build build && cd build && ctest --output-on-failure`
Expected: 全部 5 个测试可执行文件 PASS

- [ ] **Step 2: 追加性能基准测试**

在 test_PieceTable.cpp 追加：
```cpp
TEST(PieceTable, PerformanceLargeInsert) {
    PieceTable pt;
    for (int i = 0; i < 100000; ++i) {
        pt.insert(pt.length(), "X");
    }
    EXPECT_EQ(pt.length(), 100000);
}
```

- [ ] **Step 3: 运行验证通过**
- [ ] **Step 4: Commit**

```bash
git add tests/
git commit -m "test: 补充边界情况和性能基准测试"
```

---

### Task 10: README + 清理

**Files:**
- Create: `README.md`

- [ ] **Step 1: 创建 README.md**

```markdown
# SimpleMarkdown

轻量、高性能的跨平台 Markdown 编辑器。

## 构建

\```bash
git clone --recursive <repo-url>
cd easy_markdown
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
\```

## 技术栈

- C++17 / Qt 5.15 / CMake >= 3.16
- cmark-gfm (Markdown 解析)
- KSyntaxHighlighting (代码高亮)
- Google Test (单元测试)
```

- [ ] **Step 2: 最终全量测试**

Run: `cmake --build build && cd build && ctest --output-on-failure`
Expected: ALL PASS

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit -m "docs: 添加README和构建说明"
```
