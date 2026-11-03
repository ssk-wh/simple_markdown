// src/viewer/ChunkScanner.cpp
//
// Spec: specs/模块-viewer/01-分块扫描.md
// Invariants enforced here: INV-SCAN-SAFE-CUT, INV-SCAN-HARD-CAP, INV-SCAN-TOC
// Last synced: 2026-11-02
#include "ChunkScanner.h"

#include <algorithm>
#include <cstring>

namespace viewer {

namespace {

// 行内容视图（不含行尾 \n / \r）
struct LineView {
    const char* p;
    uint64_t len;
};

inline bool isSpaceTab(char c) { return c == ' ' || c == '\t'; }

// 行首缩进列数（tab 按 4 对齐）；outOff 为首个非空白字符的偏移
int leadingIndent(const LineView& l, uint64_t& outOff)
{
    int col = 0;
    uint64_t i = 0;
    for (; i < l.len; ++i) {
        if (l.p[i] == ' ') ++col;
        else if (l.p[i] == '\t') col += 4 - (col % 4);
        else break;
    }
    outOff = i;
    return col;
}

bool startsWith(const LineView& l, uint64_t off, const char* lit)
{
    const size_t n = std::strlen(lit);
    return l.len >= off + n && std::memcmp(l.p + off, lit, n) == 0;
}

bool startsWithNoCase(const LineView& l, uint64_t off, const char* lit)
{
    const size_t n = std::strlen(lit);
    if (l.len < off + n) return false;
    for (size_t i = 0; i < n; ++i) {
        char a = l.p[off + i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (a != lit[i]) return false;
    }
    return true;
}

// 是否为列表项起始（"- " "* " "+ " "1. " "1) "，含只有标记无内容的行）
bool isListMarker(const LineView& l, uint64_t off)
{
    if (off >= l.len) return false;
    const char c = l.p[off];
    if (c == '-' || c == '*' || c == '+') {
        return off + 1 >= l.len || isSpaceTab(l.p[off + 1]);
    }
    if (c >= '0' && c <= '9') {
        uint64_t i = off;
        while (i < l.len && l.p[i] >= '0' && l.p[i] <= '9' && i - off < 9) ++i;
        if (i < l.len && (l.p[i] == '.' || l.p[i] == ')')) {
            return i + 1 >= l.len || isSpaceTab(l.p[i + 1]);
        }
    }
    return false;
}

// 围栏起始：返回围栏字符与长度（0 表示不是）
bool parseFenceOpen(const LineView& l, uint64_t off, char& ch, int& len)
{
    if (off >= l.len) return false;
    const char c = l.p[off];
    if (c != '`' && c != '~') return false;
    uint64_t i = off;
    while (i < l.len && l.p[i] == c) ++i;
    const int n = static_cast<int>(i - off);
    if (n < 3) return false;
    if (c == '`') {
        // 反引号围栏的 info 串不得再含反引号
        for (uint64_t k = i; k < l.len; ++k)
            if (l.p[k] == '`') return false;
    }
    ch = c;
    len = n;
    return true;
}

bool isFenceClose(const LineView& l, uint64_t off, char ch, int minLen)
{
    uint64_t i = off;
    while (i < l.len && l.p[i] == ch) ++i;
    if (static_cast<int>(i - off) < minLen) return false;
    for (; i < l.len; ++i)
        if (!isSpaceTab(l.p[i])) return false;
    return true;
}

// 仅由 ch 字符（可含尾随空白）构成的行，用于 setext 下划线
bool isRunOf(const LineView& l, uint64_t off, char ch)
{
    uint64_t i = off;
    while (i < l.len && l.p[i] == ch) ++i;
    if (i == off) return false;
    for (; i < l.len; ++i)
        if (!isSpaceTab(l.p[i])) return false;
    return true;
}

// 去掉首尾空白后恰好等于 "---"
bool isTrimmedDashes(const LineView& l)
{
    uint64_t a = 0, b = l.len;
    while (a < b && (isSpaceTab(l.p[a]) || l.p[a] == '\r')) ++a;
    while (b > a && (isSpaceTab(l.p[b - 1]) || l.p[b - 1] == '\r')) --b;
    return b - a == 3 && std::memcmp(l.p + a, "---", 3) == 0;
}

bool isBlankLine(const LineView& l)
{
    for (uint64_t i = 0; i < l.len; ++i)
        if (!isSpaceTab(l.p[i]) && l.p[i] != '\r') return false;
    return true;
}

// 引用式链接定义： [id]: dest
bool isRefDef(const LineView& l, uint64_t off)
{
    if (off >= l.len || l.p[off] != '[') return false;
    if (off + 1 < l.len && l.p[off + 1] == '^') return false;  // 脚注定义不处理
    uint64_t i = off + 1;
    while (i < l.len && l.p[i] != ']') {
        if (l.p[i] == '\\') ++i;
        ++i;
    }
    if (i + 1 >= l.len || l.p[i] != ']' || l.p[i + 1] != ':') return false;
    i += 2;
    while (i < l.len && isSpaceTab(l.p[i])) ++i;
    return i < l.len;
}

// HTML 原始块（内部允许空行）起始：返回结束标记，空串表示不是
std::string rawHtmlEndMarker(const LineView& l, uint64_t off)
{
    if (off >= l.len || l.p[off] != '<') return {};
    if (startsWith(l, off, "<!--")) return "-->";
    if (startsWith(l, off, "<?")) return "?>";
    if (startsWith(l, off, "<![CDATA[")) return "]]>";
    if (startsWithNoCase(l, off, "<script")) return "</script>";
    if (startsWithNoCase(l, off, "<pre")) return "</pre>";
    if (startsWithNoCase(l, off, "<style")) return "</style>";
    if (startsWithNoCase(l, off, "<textarea")) return "</textarea>";
    return {};
}

bool lineContainsNoCase(const LineView& l, const std::string& needle)
{
    if (needle.empty() || l.len < needle.size()) return false;
    for (uint64_t i = 0; i + needle.size() <= l.len; ++i) {
        size_t k = 0;
        for (; k < needle.size(); ++k) {
            char a = l.p[i + k];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (a != needle[k]) break;
        }
        if (k == needle.size()) return true;
    }
    return false;
}

}  // namespace

ChunkScanner::ChunkScanner(ScanOptions opt) : m_opt(opt) {}

void ChunkScanner::openChunk(uint64_t startByte, uint32_t startLine, uint32_t prologue, uint8_t flags)
{
    m_open = true;
    m_curStart = startByte;
    m_curStartLine = startLine;
    m_curLines = 0;
    m_curPrologue = prologue;
    m_curFlags = flags;
}

void ChunkScanner::closeChunk(uint64_t endByte, uint8_t extraFlags)
{
    if (!m_open) return;
    if (endByte > m_curStart || m_curLines > 0) {
        Chunk c;
        c.byteStart = m_curStart;
        c.byteEnd = endByte;
        c.lineStart = m_curStartLine;
        c.lineCount = m_curLines;
        c.prologueIdx = m_curPrologue;
        c.flags = static_cast<uint8_t>(m_curFlags | extraFlags);
        m_chunks.push_back(c);
    }
    m_open = false;
}

bool ChunkScanner::scan(const char* data, uint64_t available, bool final)
{
    // 跳过 UTF-8 BOM
    if (!m_started) {
        m_started = true;
        if (available >= 3 && static_cast<uint8_t>(data[0]) == 0xEF
            && static_cast<uint8_t>(data[1]) == 0xBB && static_cast<uint8_t>(data[2]) == 0xBF) {
            m_pos = 3;
        }
    }

    while (m_pos < available) {
        const char* base = data + m_pos;
        const void* nl = std::memchr(base, '\n', available - m_pos);
        uint64_t lineEnd;
        uint64_t next;
        if (nl) {
            lineEnd = static_cast<uint64_t>(static_cast<const char*>(nl) - data);
            next = lineEnd + 1;
        } else {
            if (!final) break;  // 残行等待更多数据
            lineEnd = available;
            next = available;
        }
        uint64_t contentEnd = lineEnd;
        if (contentEnd > m_pos && data[contentEnd - 1] == '\r') --contentEnd;

        const uint64_t ls = m_pos;
        if (!m_open) openChunk(ls, m_lineNo, 0, 0);

        // frontmatter 判定：首个非空行（trim 后）== "---"，且 200 行内存在 trim 后为 "---" 的结束行。
        // 规则必须与 MarkdownParser::extractFrontmatter 完全一致（反模式 B：两份实现必须同步）。
        if (!m_seenNonBlank && !isBlankLine(LineView{data + ls, contentEnd - ls}) &&
            isTrimmedDashes(LineView{data + ls, contentEnd - ls})) {
            uint64_t p = next;
            int n = 0;
            bool closed = false;
            while (p < available && n < 200) {
                const void* nl2 = std::memchr(data + p, '\n', available - p);
                const uint64_t e = nl2 ? static_cast<uint64_t>(static_cast<const char*>(nl2) - data) : available;
                uint64_t ce = e;
                if (ce > p && data[ce - 1] == '\r') --ce;
                if (isTrimmedDashes(LineView{data + p, ce - p})) {
                    closed = true;
                    break;
                }
                if (!nl2) break;
                p = e + 1;
                ++n;
            }
            if (closed) m_inFrontmatter = true;
        }

        if (contentEnd - ls > static_cast<uint64_t>(m_opt.maxLineBytes)) {
            sliceLongLine(data, ls, contentEnd, next);
        } else {
            processLine(data, ls, contentEnd, next);
        }
        m_pos = next;
        ++m_lineNo;
        ++m_curLines;
    }

    if (final && m_pos >= available) {
        closeChunk(available, 0);
        return true;
    }
    return false;
}

void ChunkScanner::sliceLongLine(const char* data, uint64_t ls, uint64_t le, uint64_t next)
{
    // 先收尾当前 chunk（该行之前的内容），超长行自成若干 chunk
    if (m_curLines > 0) {
        closeChunk(ls, 0);
        openChunk(ls, m_lineNo, 0, 0);
    }
    uint64_t p = ls;
    bool first = true;
    while (p < le) {
        uint64_t q = std::min<uint64_t>(le, p + static_cast<uint64_t>(m_opt.maxLineBytes));
        if (q < le) {
            // 回退到 UTF-8 字符边界
            while (q > p && (static_cast<uint8_t>(data[q]) & 0xC0) == 0x80) --q;
            if (q == p) q = std::min<uint64_t>(le, p + static_cast<uint64_t>(m_opt.maxLineBytes));
        }
        const bool last = (q >= le);
        const uint64_t end = last ? next : q;
        Chunk c;
        c.byteStart = p;
        c.byteEnd = end;
        c.lineStart = m_lineNo;
        c.lineCount = first ? 1 : 0;
        c.flags = first ? 0 : Chunk::kMidLine;
        m_chunks.push_back(c);
        first = false;
        p = q;
    }
    // 当前行已由切片消化；后续行从新 chunk 开始。processLine 不会再处理此行，
    // 因此这里手动维护状态，并抵消 scan() 中 ++m_curLines 的副作用。
    m_open = false;
    openChunk(next, m_lineNo + 1, 0, 0);
    m_curLines = static_cast<uint32_t>(-1);  // 配合 scan() 的 ++ 回到 0
    m_prevBlank = false;
    m_prevParaText = false;
}

void ChunkScanner::processLine(const char* data, uint64_t ls, uint64_t le, uint64_t next)
{
    const LineView line{data + ls, le - ls};
    uint64_t off = 0;
    const int indent = leadingIndent(line, off);
    const bool blank = isBlankLine(line);

    // 1) frontmatter 内部：只找收尾行
    if (m_inFrontmatter) {
        if (m_seenNonBlank && isTrimmedDashes(line)) m_inFrontmatter = false;
        m_seenNonBlank = true;
        m_prevBlank = false;
        m_prevParaText = false;
        return;
    }

    // 2) 围栏内部
    if (m_fenceChar) {
        // 达到强切上限：在围栏内部切开，续块补合成围栏头
        if (m_curLines >= static_cast<uint32_t>(std::min(m_opt.hardLines, m_opt.fenceHardLines)) ||
            ls - m_curStart >= static_cast<uint64_t>(m_opt.hardBytes)) {
            closeChunk(ls, Chunk::kEndsInOpenFence);
            m_prologues.push_back(m_fenceHeader);
            openChunk(ls, m_lineNo, static_cast<uint32_t>(m_prologues.size()), 0);
        }
        if (indent <= 3 && isFenceClose(line, off, m_fenceChar, m_fenceLen)) {
            m_fenceChar = 0;
            m_fenceLen = 0;
            m_fenceHeader.clear();
        }
        m_prevBlank = false;
        m_prevParaText = false;
        return;
    }

    // 3) HTML 原始块内部
    if (!m_rawEnd.empty()) {
        if (lineContainsNoCase(line, m_rawEnd)) m_rawEnd.clear();
        m_prevBlank = blank;
        m_prevParaText = false;
        return;
    }

    if (blank) {
        m_prevBlank = true;
        m_prevParaText = false;
        return;
    }
    const bool firstNonBlank = !m_seenNonBlank;
    m_seenNonBlank = true;

    // 4) 切块判定：空行之后、顶格、非列表项
    const bool listMarker = isListMarker(line, off);
    if (m_prevBlank && indent == 0 && m_curLines > 0 && !firstNonBlank) {
        bool cut = false;
        if (!listMarker && m_curLines >= static_cast<uint32_t>(m_opt.targetLines)) cut = true;
        // 强切：超过硬上限时，列表允许在顶层项之间切开
        if (!cut && listMarker &&
            (m_curLines >= static_cast<uint32_t>(m_opt.hardLines) ||
             ls - m_curStart >= static_cast<uint64_t>(m_opt.hardBytes))) cut = true;
        if (cut) {
            closeChunk(ls, 0);
            openChunk(ls, m_lineNo, 0, 0);
        }
    }
    // 极端兜底：无任何安全边界且体量达到硬上限 2 倍，在任意行边界切开（可能破坏表格等结构）
    if (m_curLines >= static_cast<uint32_t>(m_opt.hardLines) * 2 ||
        ls - m_curStart >= static_cast<uint64_t>(m_opt.hardBytes) * 2) {
        closeChunk(ls, 0);
        openChunk(ls, m_lineNo, 0, 0);
    }

    // 5) 围栏开启
    char fch = 0;
    int flen = 0;
    if (indent <= 3 && parseFenceOpen(line, off, fch, flen)) {
        m_fenceChar = fch;
        m_fenceLen = flen;
        m_fenceHeader.assign(line.p + off, static_cast<size_t>(line.len - off));
        m_prevBlank = false;
        m_prevParaText = false;
        return;
    }

    // 6) ATX 标题
    if (indent <= 3 && line.p[off] == '#') {
        uint64_t i = off;
        while (i < line.len && line.p[i] == '#') ++i;
        const int lv = static_cast<int>(i - off);
        if (lv >= 1 && lv <= 6 && (i >= line.len || isSpaceTab(line.p[i]))) {
            uint64_t e = line.len;
            while (e > i && isSpaceTab(line.p[e - 1])) --e;
            // 去除闭合的 #
            uint64_t t = e;
            while (t > i && line.p[t - 1] == '#') --t;
            if (t == i || isSpaceTab(line.p[t - 1])) e = t;
            while (e > i && isSpaceTab(line.p[e - 1])) --e;
            uint64_t s = i;
            while (s < e && isSpaceTab(line.p[s])) ++s;
            HeadingEntry h;
            h.line = m_lineNo;
            h.level = static_cast<uint8_t>(lv);
            h.chunkIndex = static_cast<uint32_t>(m_chunks.size());
            h.title = stripInlineMarkup(QByteArray(line.p + s, static_cast<int>(e - s)));
            m_headings.push_back(std::move(h));
            m_prevBlank = false;
            m_prevParaText = false;
            return;
        }
    }

    // 7) setext 标题：上一行是普通文本，本行是 === 或 ---
    if (m_prevParaText && indent <= 3 && (isRunOf(line, off, '=') || isRunOf(line, off, '-'))) {
        HeadingEntry h;
        h.line = m_lineNo - 1;
        h.level = (line.p[off] == '=') ? 1 : 2;
        h.chunkIndex = static_cast<uint32_t>(m_chunks.size());
        h.title = stripInlineMarkup(m_prevLineText);
        m_headings.push_back(std::move(h));
        m_prevBlank = false;
        m_prevParaText = false;
        return;
    }

    // 8) HTML 原始块开启
    if (indent <= 3) {
        std::string endMarker = rawHtmlEndMarker(line, off);
        if (!endMarker.empty()) {
            if (!lineContainsNoCase(LineView{line.p + off + 1, line.len - off - 1}, endMarker))
                m_rawEnd = endMarker;
            m_prevBlank = false;
            m_prevParaText = false;
            return;
        }
    }

    // 9) 引用式链接定义
    if (indent <= 3 && isRefDef(line, off) && m_refDefCount < 2000 && m_refDefs.size() < (1 << 20)) {
        m_refDefs.append(line.p + off, static_cast<int>(line.len - off));
        m_refDefs.append('\n');
        ++m_refDefCount;
        m_prevBlank = false;
        m_prevParaText = false;
        return;
    }

    // 10) 普通行
    m_prevBlank = false;
    const bool quote = (line.p[off] == '>');
    const bool tableRow = (line.p[off] == '|');
    if (indent <= 3 && !listMarker && !quote && !tableRow) {
        m_prevParaText = true;
        m_prevLineText = QByteArray(line.p + off, static_cast<int>(line.len - off));
    } else {
        m_prevParaText = false;
    }
    (void)next;
}

AssembledChunk ChunkScanner::assemble(const char* data, const Chunk& c) const
{
    return assembleChunk(data, c, m_prologues, m_refDefs);
}

AssembledChunk ChunkScanner::assembleChunk(const char* data, const Chunk& c,
                                           const std::vector<std::string>& m_prologues,
                                           const QByteArray& m_refDefs)
{
    AssembledChunk out;
    QByteArray& t = out.text;
    t.reserve(static_cast<int>(c.byteEnd - c.byteStart) + m_refDefs.size() + 64);
    if (c.prologueIdx > 0 && c.prologueIdx <= m_prologues.size()) {
        const std::string& h = m_prologues[c.prologueIdx - 1];
        t.append(h.data(), static_cast<int>(h.size()));
        t.append('\n');
        out.lineBias = 1;
    }
    t.append(data + c.byteStart, static_cast<int>(c.byteEnd - c.byteStart));
    if (c.flags & Chunk::kEndsInOpenFence) {
        if (t.isEmpty() || t.back() != '\n') t.append('\n');
        // 闭合围栏：使用不会被内容截断的足够长围栏
        t.append("`````\n~~~~~\n");
    } else if (c.flags & Chunk::kMidLine) {
        // 续片：保证以换行结尾
        if (t.isEmpty() || t.back() != '\n') t.append('\n');
    }
    if (!m_refDefs.isEmpty()) {
        if (t.isEmpty() || t.back() != '\n') t.append('\n');
        t.append('\n');
        t.append(m_refDefs);
    }
    return out;
}

QString ChunkScanner::stripInlineMarkup(const QByteArray& raw)
{
    QString s = QString::fromUtf8(raw);
    QString out;
    out.reserve(s.size());
    const int n = s.size();
    for (int i = 0; i < n; ++i) {
        const QChar c = s[i];
        if (c == '\\' && i + 1 < n) {          // 转义
            out.append(s[++i]);
        } else if (c == '`' || c == '*' || c == '~') {
            continue;
        } else if (c == '_') {
            // 单词内部的下划线保留（snake_case）
            const bool prevAlnum = i > 0 && s[i - 1].isLetterOrNumber();
            const bool nextAlnum = i + 1 < n && s[i + 1].isLetterOrNumber();
            if (!(prevAlnum && nextAlnum)) continue;
            out.append(c);
        } else if (c == '!' && i + 1 < n && s[i + 1] == '[') {
            continue;                           // 图片标记的 !
        } else if (c == '[') {
            continue;
        } else if (c == ']') {
            // 跳过 ](url) 或 ][ref]
            if (i + 1 < n && (s[i + 1] == '(' || s[i + 1] == '[')) {
                const QChar close = (s[i + 1] == '(') ? QChar(')') : QChar(']');
                const int e = s.indexOf(close, i + 2);
                if (e >= 0) { i = e; continue; }
            }
        } else {
            out.append(c);
        }
    }
    return out.trimmed();
}

}  // namespace viewer
