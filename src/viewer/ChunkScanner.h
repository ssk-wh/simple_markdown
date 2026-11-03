// src/viewer/ChunkScanner.h
//
// Spec: specs/模块-viewer/01-分块扫描.md
// Invariants enforced here: INV-SCAN-SAFE-CUT, INV-SCAN-HARD-CAP, INV-SCAN-TOC
// Last synced: 2026-11-02
#pragma once

#include <QByteArray>
#include <QString>
#include <cstdint>
#include <string>
#include <vector>

namespace viewer {

// 一个 chunk = 源文件中一段连续字节，可独立交给 cmark 解析与布局。
struct Chunk {
    enum Flag : uint8_t {
        kEndsInOpenFence = 1,  // 强切在围栏代码块内部：组装时补闭合围栏
        kMidLine = 2,          // 起点位于一行中间（超长行按字节切分的续片）
    };
    uint64_t byteStart = 0;
    uint64_t byteEnd = 0;       // 不含
    uint32_t lineStart = 0;     // 0 基起始行号
    uint32_t lineCount = 0;     // 覆盖的物理行数（续片为 0）
    uint32_t prologueIdx = 0;   // 0 = 无；否则为 prologues()[idx-1]（围栏头，续切代码块用）
    uint8_t flags = 0;
};

struct HeadingEntry {
    uint32_t line = 0;          // 0 基行号
    uint8_t level = 1;
    uint32_t chunkIndex = 0;
    QString title;              // 已去除常见行内标记的纯文本
};

// 组装后的 chunk 解析输入
struct AssembledChunk {
    QByteArray text;            // UTF-8，可直接喂给 cmark
    int lineBias = 0;           // 解析结果行号（1 基）需 + chunk.lineStart - lineBias 才是文件 1 基行号
};

struct ScanOptions {
    int targetLines = 40;           // 达到该行数后在安全边界切块
    // 强切上限按「单 chunk 布局不超过 ~20ms」取值（R4 病态输入探测：逐字符换行布局约 1µs/字符）
    int hardLines = 600;            // 超过后启用强切（2 倍时在任意行边界切）
    int hardBytes = 64 * 1024;      // 超过后启用强切（2 倍时在任意行边界切）
    int fenceHardLines = 300;       // 围栏代码块内部强切行数（强切无损：续块合成围栏头）
    int maxLineBytes = 8 * 1024;    // 单行超过该长度按字节切片
};

// 增量线性扫描器：非线程安全，由单个 worker 持有。
// 用法：反复调用 scan(data, size, available, final)；data 指向的缓冲在整个扫描期间不可移动。
class ChunkScanner {
public:
    explicit ChunkScanner(ScanOptions opt = {});

    // 处理 [当前位置, available) 内的完整行；final=true 时包含末尾无换行的残行。
    // 返回 true 表示整个文件已扫描完毕（仅 final 时可能为 true）。
    bool scan(const char* data, uint64_t available, bool final);

    const std::vector<Chunk>& chunks() const { return m_chunks; }
    const std::vector<HeadingEntry>& headings() const { return m_headings; }
    const std::vector<std::string>& prologues() const { return m_prologues; }
    // 全文引用式链接定义（"[id]: url" 行），附加到每个 chunk 解析输入末尾
    const QByteArray& refDefs() const { return m_refDefs; }
    uint64_t scannedBytes() const { return m_pos; }
    uint32_t lineCount() const { return m_lineNo; }

    // 把 chunk 组装为可独立解析的 UTF-8 文本
    AssembledChunk assemble(const char* data, const Chunk& c) const;
    // 无需扫描器实例的组装（UI 线程持有 prologues/refDefs 副本即可）
    static AssembledChunk assembleChunk(const char* data, const Chunk& c,
                                        const std::vector<std::string>& prologues,
                                        const QByteArray& refDefs);

    // 去除标题文本中的常见行内 Markdown 标记（供目录显示）
    static QString stripInlineMarkup(const QByteArray& raw);

private:
    void processLine(const char* data, uint64_t ls, uint64_t le, uint64_t next);
    void closeChunk(uint64_t endByte, uint8_t flags);
    void openChunk(uint64_t startByte, uint32_t startLine, uint32_t prologue, uint8_t flags);
    void sliceLongLine(const char* data, uint64_t ls, uint64_t le, uint64_t next);

    ScanOptions m_opt;
    uint64_t m_pos = 0;
    uint32_t m_lineNo = 0;

    std::vector<Chunk> m_chunks;
    std::vector<HeadingEntry> m_headings;
    std::vector<std::string> m_prologues;
    QByteArray m_refDefs;
    int m_refDefCount = 0;

    // 当前打开 chunk
    bool m_open = false;
    uint64_t m_curStart = 0;
    uint32_t m_curStartLine = 0;
    uint32_t m_curLines = 0;
    uint32_t m_curPrologue = 0;
    uint8_t m_curFlags = 0;

    // 状态
    bool m_started = false;
    bool m_seenNonBlank = false;     // 已见到首个非空行（此前不允许切块，保证 frontmatter 与其前导空行同属首块）
    bool m_prevBlank = true;
    bool m_prevParaText = false;     // 上一行是可成为 setext 标题的普通文本行
    QByteArray m_prevLineText;       // 上一行原文（setext 标题用）
    bool m_inFrontmatter = false;
    char m_fenceChar = 0;
    int m_fenceLen = 0;
    std::string m_fenceHeader;       // 完整围栏头（缩进已去除）
    std::string m_rawEnd;            // 非空 = 处于 HTML 原始块（script/pre/注释…）内
};

}  // namespace viewer
