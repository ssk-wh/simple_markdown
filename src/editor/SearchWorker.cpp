#include "SearchWorker.h"
#include <QRegularExpression>

SearchWorker::SearchWorker(QObject* parent)
    : QObject(parent)
{
}

void SearchWorker::search(const QString& text, const QString& fullText, int requestId)
{
    searchWithOptions(text, fullText, requestId, false, false, false);
}

void SearchWorker::searchWithOptions(const QString& text, const QString& fullText, int requestId,
                                     bool caseSensitive, bool wholeWord, bool regex)
{
    emit searchFinished(findMatches(text, fullText, caseSensitive, wholeWord, regex), requestId);
}

QVector<QPair<int,int>> SearchWorker::findMatches(const QString& text, const QString& fullText,
                                                  bool caseSensitive, bool wholeWord, bool regex)
{
    QVector<QPair<int,int>> matches;

    if (text.isEmpty()) {
        return matches;
    }

    if (regex) {
        // 正则表达式搜索
        QRegularExpression::PatternOptions options = QRegularExpression::MultilineOption;
        if (!caseSensitive)
            options |= QRegularExpression::CaseInsensitiveOption;

        QRegularExpression re(text, options);
        if (!re.isValid()) {
            return matches;
        }

        QRegularExpressionMatchIterator it = re.globalMatch(fullText);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            matches.append({match.capturedStart(), match.capturedLength()});
        }
    } else if (wholeWord) {
        // 全词匹配
        Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        int pos = 0;
        int len = text.length();

        while ((pos = fullText.indexOf(text, pos, cs)) != -1) {
            // 检查前后是否为单词边界（字母、数字、下划线视为单词字符）
            auto isWordChar = [](QChar c) { return c.isLetterOrNumber() || c == '_'; };
            bool validStart = (pos == 0 || !isWordChar(fullText[pos - 1]));
            bool validEnd = (pos + len >= fullText.length() || !isWordChar(fullText[pos + len]));

            if (validStart && validEnd) {
                matches.append({pos, len});
            }
            pos += len;
        }
    } else {
        // 普通搜索
        Qt::CaseSensitivity cs = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        int pos = 0;
        int len = text.length();

        while ((pos = fullText.indexOf(text, pos, cs)) != -1) {
            matches.append({pos, len});
            pos += len;
        }
    }

    return matches;
}
