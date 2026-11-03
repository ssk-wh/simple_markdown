// src/viewer/BufferSearch.cpp
//
// Spec: specs/模块-viewer/05-查找.md
// Last synced: 2026-11-02
#include "BufferSearch.h"

#include <cstring>
#include <functional>
#include <QMetaObject>
#include <QThread>

namespace viewer {

namespace {
inline unsigned char lower(unsigned char c) { return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c + 32) : c; }
inline unsigned char upper(unsigned char c) { return (c >= 'a' && c <= 'z') ? static_cast<unsigned char>(c - 32) : c; }
}  // namespace

std::vector<uint64_t> findAllBytes(const char* data, uint64_t size, const QByteArray& needle,
                                   bool caseSensitive, size_t maxHits, const std::atomic<bool>* cancel)
{
    std::vector<uint64_t> hits;
    const uint64_t n = static_cast<uint64_t>(needle.size());
    if (!data || n == 0 || size < n) return hits;

    const unsigned char first = static_cast<unsigned char>(needle[0]);
    const unsigned char firstLo = caseSensitive ? first : lower(first);
    const unsigned char firstUp = caseSensitive ? first : upper(first);
    const char* nd = needle.constData();

    uint64_t i = 0;
    const uint64_t last = size - n;
    uint64_t sinceCheck = 0;
    while (i <= last) {
        // 找下一个首字节候选：大小写两种形式各 memchr 一次，取较近者
        const char* base = data + i;
        const uint64_t span = last - i + 1;
        const void* a = std::memchr(base, firstLo, span);
        const void* b = (firstUp != firstLo) ? std::memchr(base, firstUp, span) : nullptr;
        const char* p = static_cast<const char*>(a);
        if (b && (!p || static_cast<const char*>(b) < p)) p = static_cast<const char*>(b);
        if (!p) break;
        i = static_cast<uint64_t>(p - data);

        bool ok = true;
        if (caseSensitive) {
            ok = std::memcmp(data + i, nd, n) == 0;
        } else {
            for (uint64_t k = 1; k < n; ++k) {
                if (lower(static_cast<unsigned char>(data[i + k])) != lower(static_cast<unsigned char>(nd[k]))) {
                    ok = false;
                    break;
                }
            }
        }
        if (ok) {
            hits.push_back(i);
            if (hits.size() >= maxHits) break;
            i += n;                      // 不重叠
        } else {
            ++i;
        }
        if (cancel && ((++sinceCheck & 0x3FFF) == 0) && cancel->load(std::memory_order_relaxed)) break;
    }
    return hits;
}

namespace {
class SearchThread : public QThread {
public:
    explicit SearchThread(std::function<void()> fn) : m_fn(std::move(fn)) {}
    void run() override { m_fn(); }
private:
    std::function<void()> m_fn;
};
}  // namespace

BufferSearchTask::BufferSearchTask(std::shared_ptr<QByteArray> buffer, uint64_t size, QByteArray needle,
                                   bool caseSensitive, QObject* parent)
    : QObject(parent), m_shared(std::make_shared<Shared>()), m_buffer(std::move(buffer)),
      m_size(size), m_needle(std::move(needle)), m_cs(caseSensitive)
{
    m_shared->notifier = this;
}

BufferSearchTask::~BufferSearchTask()
{
    m_shared->cancel.store(true);
    std::lock_guard<std::mutex> lk(m_shared->mutex);
    m_shared->notifier = nullptr;
}

void BufferSearchTask::start()
{
    auto shared = m_shared;
    auto buffer = m_buffer;
    const uint64_t size = m_size;
    const QByteArray needle = m_needle;
    const bool cs = m_cs;
    auto* th = new SearchThread([shared, buffer, size, needle, cs]() {
        auto hits = findAllBytes(buffer ? buffer->constData() : nullptr, size, needle, cs, 200000, &shared->cancel);
        std::lock_guard<std::mutex> lk(shared->mutex);
        if (shared->cancel.load() || !shared->notifier) return;
        shared->hits = std::move(hits);
        QMetaObject::invokeMethod(shared->notifier, "onWake", Qt::QueuedConnection);
    });
    connect(th, &QThread::finished, th, &QObject::deleteLater);
    th->start();
}

void BufferSearchTask::onWake()
{
    m_done = true;
    emit finished();
}

std::vector<uint64_t> BufferSearchTask::takeHits()
{
    std::lock_guard<std::mutex> lk(m_shared->mutex);
    return std::move(m_shared->hits);
}

}  // namespace viewer
