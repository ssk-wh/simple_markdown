#pragma once
#include <QString>
#include <QVector>
#include <QTextCharFormat>
#include <QColor>
#include <vector>

#include "Theme.h"

struct HighlightToken {
    int start;
    int length;
    QTextCharFormat format;
};

class SyntaxHighlighter {
public:
    SyntaxHighlighter();

    void setTheme(const Theme& theme);

    QVector<HighlightToken> highlightLine(int lineIndex, const QString& text);
    void invalidateFromLine(int startLine);
    void setLineCount(int count);

private:
    enum State { Normal = 0, InCodeBlock = 1 };

    struct LineState {
        State state = Normal;
    };
    std::vector<LineState> m_states;

    QVector<HighlightToken> highlightNormal(const QString& text);
    QVector<HighlightToken> highlightCodeBlock(const QString& text);

    // 预定义格式
    QTextCharFormat m_headingFormat;
    QTextCharFormat m_boldFormat;
    QTextCharFormat m_italicFormat;
    QTextCharFormat m_codeFormat;
    QTextCharFormat m_linkFormat;
    QTextCharFormat m_codeBlockFormat;
    QTextCharFormat m_listFormat;
    QTextCharFormat m_blockQuoteFormat;
    QTextCharFormat m_fenceFormat;

    void setupFormats(const Theme& theme);
};
