#pragma once

#include <QObject>
#include <QTimer>
#include <QThread>
#include <memory>
#include "MarkdownAst.h"

class Document;
class MarkdownParser;

class ParseWorker : public QObject {
    Q_OBJECT
public slots:
    void doParse(const QString& text, quint64 seq);
signals:
    void parseFinished(std::shared_ptr<AstNode> root, quint64 seq);
private:
    std::unique_ptr<MarkdownParser> m_parser;
};

class ParseScheduler : public QObject {
    Q_OBJECT
public:
    explicit ParseScheduler(QObject* parent = nullptr);
    ~ParseScheduler();

    void setDocument(Document* doc);

    // 手动触发一次解析（用于初始加载）
    void parseNow();

signals:
    void astReady(std::shared_ptr<AstNode> root);

private slots:
    void onTextChanged();
    void onParseFinished(std::shared_ptr<AstNode> root, quint64 seq);

private:
    Document* m_doc = nullptr;
    QTimer m_debounceTimer;
    quint64 m_editSeq = 0;
    QThread m_parseThread;
    ParseWorker* m_worker = nullptr;

    void requestParse();
};
