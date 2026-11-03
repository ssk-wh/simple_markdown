// src/viewer/BufferSearch.h
//
// Spec: specs/模块-viewer/05-查找.md
// Invariants enforced here: INV-FIND-SOURCE-CANDIDATES, INV-FIND-CANCELLABLE
// Last synced: 2026-11-02
#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

#include <QByteArray>
#include <QObject>
#include <memory>
#include <mutex>

namespace viewer {

// 在 UTF-8 源缓冲上做子串搜索，返回命中的字节偏移（升序，互不重叠）。
// - caseSensitive=false 时仅对 ASCII 做大小写折叠（非 ASCII 字符按字节精确匹配，CJK 无大小写）。
// - maxHits 限制结果数量，防止在海量命中（如搜索单个字母）时占用过多内存。
// - cancel 被置位后尽快返回已找到的部分结果。
std::vector<uint64_t> findAllBytes(const char* data, uint64_t size, const QByteArray& needle,
                                   bool caseSensitive, size_t maxHits,
                                   const std::atomic<bool>* cancel = nullptr);

// 后台搜索任务：专用 QThread（不用全局线程池，见 Spec 03 INV-LOAD-NOPOOL），
// 销毁时只置取消标志，不阻塞 UI；结果通过 finished() 通知后由 takeHits() 取走。
class BufferSearchTask : public QObject {
    Q_OBJECT
public:
    BufferSearchTask(std::shared_ptr<QByteArray> buffer, uint64_t size, QByteArray needle,
                     bool caseSensitive, QObject* parent = nullptr);
    ~BufferSearchTask() override;
    void start();
    std::vector<uint64_t> takeHits();
    bool isDone() const { return m_done; }

signals:
    void finished();

private slots:
    void onWake();

private:
    struct Shared {
        std::atomic<bool> cancel{false};
        std::mutex mutex;
        std::vector<uint64_t> hits;
        QObject* notifier = nullptr;
    };
    std::shared_ptr<Shared> m_shared;
    std::shared_ptr<QByteArray> m_buffer;
    uint64_t m_size;
    QByteArray m_needle;
    bool m_cs;
    bool m_done = false;
};

}  // namespace viewer
