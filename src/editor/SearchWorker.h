#pragma once
#include <QObject>
#include <QVector>
#include <QPair>
#include <QString>

class SearchWorker : public QObject {
    Q_OBJECT
public:
    explicit SearchWorker(QObject* parent = nullptr);
    static QVector<QPair<int,int>> findMatches(const QString& text, const QString& fullText,
                                               bool caseSensitive, bool wholeWord, bool regex);

public slots:
    void search(const QString& text, const QString& fullText, int requestId);
    void searchWithOptions(const QString& text, const QString& fullText, int requestId,
                          bool caseSensitive, bool wholeWord, bool regex);

signals:
    void searchFinished(QVector<QPair<int,int>> matches, int requestId);
};
