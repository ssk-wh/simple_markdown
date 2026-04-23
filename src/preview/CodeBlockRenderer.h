#pragma once

#include <QString>
#include <QColor>
#include <QRegularExpression>
#include <vector>

class CodeBlockRenderer {
public:
    struct HighlightedSegment {
        QString text;
        QColor color;
        bool bold = false;
    };

    using HighlightedLine = std::vector<HighlightedSegment>;

    CodeBlockRenderer();
    ~CodeBlockRenderer();

    std::vector<HighlightedLine> highlight(const QString& code, const QString& language, bool isDark);

private:
    enum TokenType { Default, Keyword, String, Comment, Number, Type, Preprocessor };

    struct LangDef {
        QRegularExpression keywords;
        QRegularExpression types;
        QRegularExpression singleComment;  // 如 //
        QString blockCommentStart;
        QString blockCommentEnd;
        bool hasPreprocessor = false;
    };

    LangDef getLangDef(const QString& language);
    QColor colorForToken(TokenType type, bool isDark);
    HighlightedLine tokenizeLine(const QString& line, const LangDef& def, bool isDark, bool& inBlockComment);
};
