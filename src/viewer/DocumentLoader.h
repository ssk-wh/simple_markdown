// src/viewer/DocumentLoader.h
//
// Spec: specs/模块-viewer/03-异步加载.md
// Invariants enforced here: INV-LOAD-NONBLOCKING, INV-LOAD-GEN, INV-LOAD-WATERMARK
// Last synced: 2026-11-02
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <QThread>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "ChunkScanner.h"

namespace viewer {

// worker → UI 的一批增量结果（UI 线程独占消费）
struct LoadBatch {
    std::vector<Chunk> chunks;
    std::vector<HeadingEntry> headings;
    std::vector<std::string> newPrologues;
    QByteArray refDefs;          // 全量快照（很小）
    uint64_t watermark = 0;      // 此批发布时缓冲中「已写完」的字节数；UI 只能读水位线以下
    uint32_t lineCount = 0;
    bool done = false;
    QString error;               // 非空表示失败（二进制/过大/读失败…）
};

// 跨线程共享状态；UI 与 worker 各持 shared_ptr，取消后 worker 自行收尾，无需 wait。
struct LoadState {
    std::shared_ptr<QByteArray> buffer;   // 一次性预分配，worker 渐进填充
    std::atomic<bool> cancelled{false};
    std::mutex mutex;
    std::vector<LoadBatch> pending;       // 受 mutex 保护
    QObject* notifier = nullptr;          // 受 mutex 保护；UI 销毁 loader 时置空
    QString path;
    qint64 fileSize = 0;
    QDateTime mtime;
};

// 文件异步加载：打开 → 读取（分段）→ 扫描 → 以批次形式发布。
// 所有 public 方法只能在 UI 线程调用。
class DocumentLoader : public QObject {
    Q_OBJECT
public:
    static constexpr qint64 kMaxFileBytes = 1024LL * 1024 * 1024;  // 1GB 上限

    explicit DocumentLoader(QObject* parent = nullptr);
    ~DocumentLoader() override;

    void start(const QString& path, ScanOptions opt = {});
    void cancel();

    // 取走累计的批次（UI 线程）
    std::vector<LoadBatch> takeBatches();

    const std::shared_ptr<LoadState>& state() const { return m_state; }
    const char* data() const { return m_state && m_state->buffer ? m_state->buffer->constData() : nullptr; }
    uint64_t bufferSize() const { return m_state && m_state->buffer ? static_cast<uint64_t>(m_state->buffer->size()) : 0; }
    QString path() const { return m_state ? m_state->path : QString(); }

    // 同步版（测试/工具用）：阻塞读完并扫描，返回完整结果
    static LoadBatch loadSync(const QString& path, std::shared_ptr<QByteArray>& bufferOut,
                              ScanOptions opt = {});

signals:
    // 有新批次可取（可能被合并，UI 应 takeBatches() 取全部）
    void batchesAvailable();

private slots:
    void onWake();

private:
    std::shared_ptr<LoadState> m_state;
    std::atomic<bool> m_wakePending{false};
    QThread* m_thread = nullptr;  // 仅用于标识；生命周期由线程自身 finished→deleteLater 管理
};

}  // namespace viewer
