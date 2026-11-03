// src/viewer/DocumentLoader.cpp
//
// Spec: specs/模块-viewer/03-异步加载.md
// Invariants enforced here: INV-LOAD-NONBLOCKING, INV-LOAD-GEN, INV-LOAD-WATERMARK
// Last synced: 2026-11-02
#include "DocumentLoader.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>
#include <QTextCodec>
#include <algorithm>
#include <cstring>
#include <functional>
#include <new>

namespace viewer {

namespace {

// 专用加载线程：不使用全局 QThreadPool，避免进程退出时线程池析构与已被系统终止的工作线程死锁
class LoaderThread : public QThread {
public:
    explicit LoaderThread(std::function<void()> fn) : m_fn(std::move(fn)) {}
    void run() override { m_fn(); }
private:
    std::function<void()> m_fn;
};

// 严格 UTF-8 校验；allowTruncatedTail=true 时容忍末尾被截断的多字节序列（分段读取）
bool isValidUtf8(const char* p, size_t n, bool allowTruncatedTail)
{
    size_t i = 0;
    while (i < n) {
        const uint8_t c = static_cast<uint8_t>(p[i]);
        if (c < 0x80) { ++i; continue; }
        size_t need = 0;
        if (c >= 0xC2 && c <= 0xDF) need = 1;
        else if (c >= 0xE0 && c <= 0xEF) need = 2;
        else if (c >= 0xF0 && c <= 0xF4) need = 3;
        else return false;
        // 需要 i+1 .. i+need 共 need 个续字节；不足则为尾部截断
        for (size_t k = 1; k <= need; ++k) {
            if (i + k >= n) return allowTruncatedTail;
            if ((static_cast<uint8_t>(p[i + k]) & 0xC0) != 0x80) return false;
        }
        i += need + 1;
    }
    return true;
}

// 非 UTF-8 文件整体转码为 UTF-8；失败返回空
QByteArray transcodeToUtf8(const QByteArray& raw)
{
    if (raw.size() >= 2) {
        const uint8_t b0 = static_cast<uint8_t>(raw[0]);
        const uint8_t b1 = static_cast<uint8_t>(raw[1]);
        if ((b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF)) {
            QTextCodec* c = QTextCodec::codecForName((b0 == 0xFF) ? "UTF-16LE" : "UTF-16BE");
            if (c) {
                QTextCodec::ConverterState st(QTextCodec::IgnoreHeader);
                return c->toUnicode(raw.constData() + 2, raw.size() - 2, &st).toUtf8();
            }
        }
    }
    QTextCodec* gb = QTextCodec::codecForName("GB18030");
    if (!gb) gb = QTextCodec::codecForLocale();
    return gb->toUnicode(raw).toUtf8();
}

bool needsTranscode(const char* p, size_t n)
{
    if (n >= 2) {
        const uint8_t b0 = static_cast<uint8_t>(p[0]);
        const uint8_t b1 = static_cast<uint8_t>(p[1]);
        if ((b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF)) return true;
    }
    return !isValidUtf8(p, n, true);
}

bool looksBinary(const char* p, size_t n)
{
    // UTF-16 BOM 文件含大量 NUL，先排除
    if (n >= 2) {
        const uint8_t b0 = static_cast<uint8_t>(p[0]);
        const uint8_t b1 = static_cast<uint8_t>(p[1]);
        if ((b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF)) return false;
    }
    return std::memchr(p, '\0', std::min<size_t>(n, 8192)) != nullptr;
}

struct Publisher {
    LoadState& st;
    size_t chunkSent = 0;
    size_t headingSent = 0;
    size_t prologueSent = 0;

    void publish(const ChunkScanner& sc, uint64_t watermark, bool done, const QString& error,
                 const std::function<void(LoadState&)>& notify)
    {
        LoadBatch b;
        const auto& cs = sc.chunks();
        b.chunks.assign(cs.begin() + chunkSent, cs.end());
        chunkSent = cs.size();
        const auto& hs = sc.headings();
        b.headings.assign(hs.begin() + headingSent, hs.end());
        headingSent = hs.size();
        const auto& ps = sc.prologues();
        b.newPrologues.assign(ps.begin() + prologueSent, ps.end());
        prologueSent = ps.size();
        b.refDefs = sc.refDefs();
        b.watermark = watermark;
        b.lineCount = sc.lineCount();
        b.done = done;
        b.error = error;
        {
            std::lock_guard<std::mutex> lk(st.mutex);
            st.pending.push_back(std::move(b));
        }
        notify(st);
    }
};

// 执行加载。sync=true 时不通知 UI，批次直接返回（测试/工具）。
void runLoad(const std::shared_ptr<LoadState>& stp, ScanOptions opt,
             const std::function<void(LoadState&)>& notify)
{
    LoadState& st = *stp;
    Publisher pub{st};
    ChunkScanner scanner(opt);

    auto fail = [&](const QString& msg) {
        pub.publish(scanner, 0, true, msg, notify);
    };
    try {

    QFile f(st.path);
    if (!f.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("无法打开文件：") + f.errorString());
        return;
    }
    const qint64 size = f.size();
    if (size > DocumentLoader::kMaxFileBytes) {
        fail(QStringLiteral("文件过大（超过 1GB），无法打开"));
        return;
    }
    st.fileSize = size;
    st.mtime = QFileInfo(st.path).lastModified();
    if (size == 0) {
        st.buffer = std::make_shared<QByteArray>();
        pub.publish(scanner, 0, true, QString(), notify);
        return;
    }

    auto buffer = std::make_shared<QByteArray>(static_cast<int>(size), Qt::Uninitialized);
    char* buf = buffer->data();
    st.buffer = buffer;

    uint64_t got = 0;
    size_t seg = 64 * 1024;
    bool first = true;
    bool transcoded = false;

    while (got < static_cast<uint64_t>(size)) {
        if (st.cancelled.load(std::memory_order_relaxed)) return;
        const qint64 want = std::min<qint64>(static_cast<qint64>(seg), size - static_cast<qint64>(got));
        const qint64 n = f.read(buf + got, want);
        if (n <= 0) break;  // 文件在读取期间被截断
        got += static_cast<uint64_t>(n);

        if (first) {
            first = false;
            if (looksBinary(buf, got)) {
                fail(QStringLiteral("这看起来是二进制文件，无法作为 Markdown 预览"));
                return;
            }
            if (needsTranscode(buf, got)) {
                // 非 UTF-8（UTF-16 / GBK）：读完整个文件后整体转码，替换缓冲
                const qint64 rest = size - static_cast<qint64>(got);
                QByteArray raw(buf, static_cast<int>(got));
                raw.append(f.read(rest));
                QByteArray utf8 = transcodeToUtf8(raw);
                buffer = std::make_shared<QByteArray>(std::move(utf8));
                buf = buffer->data();
                st.buffer = buffer;
                got = static_cast<uint64_t>(buffer->size());
                transcoded = true;
                scanner.scan(buf, got, true);
                pub.publish(scanner, got, true, QString(), notify);
                return;
            }
        }
        scanner.scan(buf, got, false);
        pub.publish(scanner, got, false, QString(), notify);
        if (seg < 4u * 1024 * 1024) seg *= 4;
    }
    (void)transcoded;
    // 文件被截断时 got < size：以实际读到的长度为准
    scanner.scan(buf, got, true);
    pub.publish(scanner, got, true, QString(), notify);
    } catch (const std::bad_alloc&) {
        // 1GB 级文件或转码（GBK→UTF-16→UTF-8 会放大内存）时分配失败：给出提示，不让进程 terminate
        fail(QStringLiteral("内存不足，无法打开该文件"));
    } catch (...) {
        fail(QStringLiteral("读取文件时发生未知错误"));
    }
}

}  // namespace

DocumentLoader::DocumentLoader(QObject* parent) : QObject(parent) {}

DocumentLoader::~DocumentLoader()
{
    cancel();
}

void DocumentLoader::cancel()
{
    m_thread = nullptr;  // 线程自行 deleteLater，这里只放弃所有权
    if (!m_state) return;
    m_state->cancelled.store(true);
    {
        std::lock_guard<std::mutex> lk(m_state->mutex);
        m_state->notifier = nullptr;
    }
    m_state.reset();
}

void DocumentLoader::start(const QString& path, ScanOptions opt)
{
    cancel();
    m_wakePending.store(false);
    auto st = std::make_shared<LoadState>();
    st->path = path;
    st->notifier = this;
    m_state = st;
    auto notify = [](LoadState& s) {
        std::lock_guard<std::mutex> lk(s.mutex);
        if (!s.notifier) return;
        auto* self = static_cast<DocumentLoader*>(s.notifier);
        if (!self->m_wakePending.exchange(true))
            QMetaObject::invokeMethod(self, "onWake", Qt::QueuedConnection);
    };
    auto* th = new LoaderThread([st, opt, notify]() { runLoad(st, opt, notify); });
    // 线程结束后自行回收；取消时不等待（worker 每个读取段检查取消标志后很快退出）
    connect(th, &QThread::finished, th, &QObject::deleteLater);
    m_thread = th;
    th->start();
}

void DocumentLoader::onWake()
{
    emit batchesAvailable();
}

std::vector<LoadBatch> DocumentLoader::takeBatches()
{
    std::vector<LoadBatch> out;
    if (!m_state) return out;
    m_wakePending.store(false);
    std::lock_guard<std::mutex> lk(m_state->mutex);
    out.swap(m_state->pending);
    return out;
}

LoadBatch DocumentLoader::loadSync(const QString& path, std::shared_ptr<QByteArray>& bufferOut,
                                   ScanOptions opt)
{
    auto st = std::make_shared<LoadState>();
    st->path = path;
    runLoad(st, opt, [](LoadState&) {});
    bufferOut = st->buffer;
    // 合并所有批次
    LoadBatch all;
    for (auto& b : st->pending) {
        all.chunks.insert(all.chunks.end(), b.chunks.begin(), b.chunks.end());
        all.headings.insert(all.headings.end(), b.headings.begin(), b.headings.end());
        all.newPrologues.insert(all.newPrologues.end(), b.newPrologues.begin(), b.newPrologues.end());
        all.refDefs = b.refDefs;
        all.watermark = b.watermark;
        all.lineCount = b.lineCount;
        all.done = b.done;
        if (!b.error.isEmpty()) all.error = b.error;
    }
    return all;
}

}  // namespace viewer
