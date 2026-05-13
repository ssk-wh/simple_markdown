#include "ParseScheduler.h"
#include "MarkdownParser.h"
#include "../core/Document.h"
#include "PerfProbe.h"

#include <QMetaObject>

// ---- ParseWorker ----

ParseWorker::~ParseWorker() = default;

void ParseWorker::doParse(const QString& text, quint64 seq)
{
    SM_PERF_SCOPE("parser.doParse");
    if (!m_parser)
        m_parser = std::make_unique<MarkdownParser>();
    auto root = m_parser->parse(text);
    auto shared = std::shared_ptr<AstNode>(root.release());
    emit parseFinished(shared, seq);
}

// ---- ParseScheduler ----

ParseScheduler::ParseScheduler(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<std::shared_ptr<AstNode>>("std::shared_ptr<AstNode>");
    qRegisterMetaType<quint64>("quint64");

    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(30);
    connect(&m_debounceTimer, &QTimer::timeout, this, &ParseScheduler::requestParse);

    m_worker = new ParseWorker();
    m_worker->moveToThread(&m_parseThread);
    connect(&m_parseThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &ParseWorker::parseFinished, this, &ParseScheduler::onParseFinished);
    m_parseThread.start();
}

ParseScheduler::~ParseScheduler()
{
    m_parseThread.quit();
    m_parseThread.wait();
}

void ParseScheduler::setDocument(Document* doc)
{
    if (m_doc)
        disconnect(m_doc, nullptr, this, nullptr);
    m_doc = doc;
    if (m_doc) {
        connect(m_doc, &Document::textChanged, this, [this]() { onTextChanged(); });
    }
}

void ParseScheduler::onTextChanged()
{
    m_editSeq++;
    // [plan A6 2026-05-12] 防抖窗口自适应文档规模：行数越大，单次 parse 越贵
    // （INV-4 「10k 行 < 50ms」），固定 30ms 防抖在大文档持续输入时排队累积。
    // 映射：≤1k 行 → 30ms（现状），≤5k 行 → 50ms，> 5k 行 → 80ms。
    // 用户停止输入后 80ms 内预览必更新——可接受。
    int debounceMs = 30;
    if (m_doc) {
        int lines = m_doc->lineCount();
        if (lines > 5000)       debounceMs = 80;
        else if (lines > 1000)  debounceMs = 50;
    }
    m_debounceTimer.setInterval(debounceMs);
    m_debounceTimer.start();
}

void ParseScheduler::requestParse()
{
    if (!m_doc)
        return;
    QString text = m_doc->text();
    quint64 seq = m_editSeq;
    QMetaObject::invokeMethod(m_worker, "doParse",
                              Qt::QueuedConnection,
                              Q_ARG(QString, text),
                              Q_ARG(quint64, seq));
}

void ParseScheduler::parseNow()
{
    m_debounceTimer.stop();
    requestParse();
}

void ParseScheduler::onParseFinished(std::shared_ptr<AstNode> root, quint64 seq)
{
    if (seq < m_editSeq)
        return; // 过期结果，丢弃
    emit astReady(root);
}
