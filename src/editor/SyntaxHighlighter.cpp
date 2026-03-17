#include "SyntaxHighlighter.h"
#include <QFont>
#include <QRegularExpression>
#include <QPair>

static bool isInRanges(int pos, const QVector<QPair<int,int>>& ranges)
{
    for (const auto& r : ranges) {
        if (pos >= r.first && pos < r.second)
            return true;
    }
    return false;
}

SyntaxHighlighter::SyntaxHighlighter()
{
    setupFormats();
}

void SyntaxHighlighter::setupFormats()
{
    // 标题：深蓝色 + 粗体
    m_headingFormat.setForeground(QColor("#1A237E"));
    m_headingFormat.setFontWeight(QFont::Bold);

    // 粗体
    m_boldFormat.setFontWeight(QFont::Bold);

    // 斜体
    m_italicFormat.setFontItalic(true);

    // 行内代码：灰色背景 + 红色文字
    m_codeFormat.setForeground(QColor("#C62828"));
    m_codeFormat.setBackground(QColor("#F5F5F5"));

    // 链接：蓝色 + 下划线
    m_linkFormat.setForeground(QColor("#0366D6"));
    m_linkFormat.setFontUnderline(true);

    // 代码块内容：绿色
    m_codeBlockFormat.setForeground(QColor("#2E7D32"));
    m_codeBlockFormat.setBackground(QColor("#F8F8F8"));

    // 列表标记：紫色
    m_listFormat.setForeground(QColor("#6A1B9A"));

    // 引用：灰色
    m_blockQuoteFormat.setForeground(QColor("#757575"));

    // 围栏标记本身
    m_fenceFormat.setForeground(QColor("#999999"));
    m_fenceFormat.setBackground(QColor("#F0F0F0"));
}

QVector<HighlightToken> SyntaxHighlighter::highlightLine(int lineIndex, const QString& text)
{
    // 确保 states 数组足够大
    if (lineIndex >= static_cast<int>(m_states.size())) {
        m_states.resize(lineIndex + 1);
    }

    // 获取前一行的结束状态
    LineState prevState = (lineIndex > 0) ? m_states[lineIndex - 1] : LineState{Normal};

    QVector<HighlightToken> tokens;

    // 检查是否是围栏代码块的开始/结束标记
    QString trimmed = text.trimmed();
    if (trimmed.startsWith(QStringLiteral("```"))) {
        tokens.append({0, text.length(), m_fenceFormat});
        if (prevState.state == Normal) {
            m_states[lineIndex] = {InCodeBlock};
        } else {
            m_states[lineIndex] = {Normal};
        }
        return tokens;
    }

    if (prevState.state == InCodeBlock) {
        m_states[lineIndex] = {InCodeBlock};
        return highlightCodeBlock(text);
    }

    m_states[lineIndex] = {Normal};
    return highlightNormal(text);
}

QVector<HighlightToken> SyntaxHighlighter::highlightNormal(const QString& text)
{
    QVector<HighlightToken> tokens;

    // 标题（整行）
    static QRegularExpression headingRe(QStringLiteral("^#{1,6}\\s"));
    auto m = headingRe.match(text);
    if (m.hasMatch()) {
        tokens.append({0, text.length(), m_headingFormat});
        return tokens;
    }

    // 引用（整行）
    static QRegularExpression quoteRe(QStringLiteral("^>\\s?"));
    m = quoteRe.match(text);
    if (m.hasMatch()) {
        tokens.append({0, text.length(), m_blockQuoteFormat});
        return tokens;
    }

    // 列表标记
    static QRegularExpression ulRe(QStringLiteral("^(\\s*[-*+])\\s"));
    m = ulRe.match(text);
    if (m.hasMatch()) {
        tokens.append({m.capturedStart(1), m.capturedLength(1), m_listFormat});
    }
    static QRegularExpression olRe(QStringLiteral("^(\\s*\\d+\\.)\\s"));
    m = olRe.match(text);
    if (m.hasMatch()) {
        tokens.append({m.capturedStart(1), m.capturedLength(1), m_listFormat});
    }

    // 行内代码（优先级高于粗体/斜体）
    static QRegularExpression codeRe(QStringLiteral("`([^`]+)`"));
    auto iter = codeRe.globalMatch(text);
    QVector<QPair<int,int>> codeRanges;
    while (iter.hasNext()) {
        auto match = iter.next();
        tokens.append({match.capturedStart(), match.capturedLength(), m_codeFormat});
        codeRanges.append({match.capturedStart(), match.capturedEnd()});
    }

    // 粗体
    static QRegularExpression boldRe(QStringLiteral("\\*\\*([^*]+)\\*\\*"));
    iter = boldRe.globalMatch(text);
    while (iter.hasNext()) {
        auto match = iter.next();
        if (!isInRanges(match.capturedStart(), codeRanges))
            tokens.append({match.capturedStart(), match.capturedLength(), m_boldFormat});
    }

    // 斜体
    static QRegularExpression italicRe(QStringLiteral("(?<!\\*)\\*([^*]+)\\*(?!\\*)"));
    iter = italicRe.globalMatch(text);
    while (iter.hasNext()) {
        auto match = iter.next();
        if (!isInRanges(match.capturedStart(), codeRanges))
            tokens.append({match.capturedStart(), match.capturedLength(), m_italicFormat});
    }

    // 链接
    static QRegularExpression linkRe(QStringLiteral("\\[([^\\]]+)\\]\\(([^)]+)\\)"));
    iter = linkRe.globalMatch(text);
    while (iter.hasNext()) {
        auto match = iter.next();
        if (!isInRanges(match.capturedStart(), codeRanges))
            tokens.append({match.capturedStart(), match.capturedLength(), m_linkFormat});
    }

    return tokens;
}

QVector<HighlightToken> SyntaxHighlighter::highlightCodeBlock(const QString& text)
{
    return {{0, text.length(), m_codeBlockFormat}};
}

void SyntaxHighlighter::invalidateFromLine(int startLine)
{
    if (startLine < static_cast<int>(m_states.size())) {
        m_states.resize(startLine);
    }
}

void SyntaxHighlighter::setLineCount(int count)
{
    m_states.resize(count);
}
