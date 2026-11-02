// src/viewer/ScrollModel.h
//
// Spec: specs/模块-viewer/02-滚动模型.md
// Invariants enforced here: INV-SCROLL-ANCHOR, INV-SCROLL-VISIBLE-MEASURED, INV-SCROLL-END-EXACT
// Last synced: 2026-11-02
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace viewer {

// 以「锚点 (chunk, offset)」表示滚动位置的虚拟滚动模型（纯逻辑，无 Qt 依赖，可单测）。
//
// 核心思想：文档顶部到视口顶部的距离 = prefix(anchorChunk) + anchorOffset。
// 锚点相对 chunk 自身定义，因此其上方任意 chunk 由「估高」变「实高」都不会改变屏幕上看到的内容，
// 只会让「推导出的滚动条值」变化（INV-SCROLL-ANCHOR）。
class ScrollModel {
public:
    // 返回 chunk 在当前宽度下的实际高度（由 Viewer 负责解析+布局）
    using Measurer = std::function<double(size_t chunkIndex)>;

    struct VisibleChunk {
        size_t index = 0;
        double screenY = 0;  // chunk 顶部相对视口顶部的 y（可为负）
        double height = 0;
    };

    ScrollModel();

    void setMeasurer(Measurer m) { m_measurer = std::move(m); }
    void setViewportHeight(double h) { m_viewportH = h; }
    double viewportHeight() const { return m_viewportH; }
    // 未实测 chunk 的初始每行估高（像素）
    void setInitialPxPerLine(double px);

    void reset();
    void appendChunk(uint32_t lineCount);
    size_t count() const { return m_lines.size(); }

    // 高度
    double total() const { return prefixOf(m_lines.size()); }
    double chunkTop(size_t i) const { return prefixOf(i); }
    double chunkHeight(size_t i) const { return m_h[i]; }
    bool isMeasured(size_t i) const { return m_measured[i] != 0; }
    size_t measuredCount() const { return m_measuredCount; }
    double pxPerLine() const { return m_pxPerLine; }

    // 锚点
    size_t anchorChunk() const { return m_anchorChunk; }
    double anchorOffset() const { return m_anchorOffset; }
    double topY() const;                      // 基于当前高度推导
    void jumpTo(size_t chunk, double offset = 0.0);
    void scrollBy(double dy);
    void scrollToStart();
    void scrollToEnd();
    void setTopY(double y);                   // 滚动条拖动：按当前高度反查锚点
    double maxTopY() const;

    // 宽度/字号/主题变化：所有实测失效，锚点保持
    void invalidateAll();
    // 单个 chunk 内容变化（如图片加载完成）：下次可视时重新测量
    void invalidate(size_t i);

    // 拖动滚动条期间冻结估高校准，避免整体漂移
    void setCalibrationFrozen(bool f) { m_frozen = f; }

    // 返回覆盖 [0, viewportH + bufferPx] 范围的 chunk；其中视口内的 chunk 保证已测量。
    std::vector<VisibleChunk> visible(double bufferPx = 0.0);

    // 查找 y 所在 chunk（文档坐标）
    size_t indexAtY(double y) const;

private:
    double prefixOf(size_t i) const;          // Σ h[0..i)
    void fenwickAdd(size_t i, double delta);
    void rebuildFenwick();
    double ensureMeasured(size_t i);          // 测量并写回，返回实高
    void applyMeasured(size_t i, double h);
    void maybeRecalibrate();
    double estimate(size_t i) const;
    void normalizeAnchor();

    Measurer m_measurer;
    double m_viewportH = 600.0;
    double m_pxPerLine = 24.0;
    double m_lastRebuildPxPerLine = 24.0;
    bool m_frozen = false;

    std::vector<uint32_t> m_lines;
    std::vector<double> m_h;          // 当前有效高度（实测或估算）
    std::vector<uint8_t> m_measured;
    std::vector<double> m_fen;        // 1 基 Fenwick
    size_t m_measuredCount = 0;
    double m_sumMeasuredH = 0;
    double m_sumMeasuredLines = 0;

    size_t m_anchorChunk = 0;
    double m_anchorOffset = 0;
};

}  // namespace viewer
