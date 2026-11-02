// src/viewer/ScrollModel.cpp
//
// Spec: specs/模块-viewer/02-滚动模型.md
// Invariants enforced here: INV-SCROLL-ANCHOR, INV-SCROLL-VISIBLE-MEASURED, INV-SCROLL-END-EXACT
// Last synced: 2026-11-02
#include "ScrollModel.h"

#include <algorithm>
#include <cmath>

namespace viewer {

namespace {
constexpr size_t kTailMeasureWindow = 64;   // 距末尾多少个 chunk 内开始精算尾部
constexpr double kRecalibrateRatio = 0.05;  // 估高系数相对变化超过 5% 才整体重算
}  // namespace

ScrollModel::ScrollModel()
{
    m_fen.push_back(0.0);  // Fenwick 为 1 基
}

void ScrollModel::setInitialPxPerLine(double px)
{
    if (px < 1.0) px = 1.0;
    m_pxPerLine = px;
    m_lastRebuildPxPerLine = px;
}

void ScrollModel::reset()
{
    m_lines.clear();
    m_h.clear();
    m_measured.clear();
    m_fen.assign(1, 0.0);
    m_measuredCount = 0;
    m_sumMeasuredH = 0;
    m_sumMeasuredLines = 0;
    m_anchorChunk = 0;
    m_anchorOffset = 0;
}

double ScrollModel::estimate(size_t i) const
{
    return static_cast<double>(std::max<uint32_t>(m_lines[i], 1)) * m_pxPerLine;
}

void ScrollModel::appendChunk(uint32_t lineCount)
{
    m_lines.push_back(lineCount);
    const double h = estimate(m_lines.size() - 1);
    m_h.push_back(h);
    m_measured.push_back(0);
    // Fenwick 末尾追加：tree[n] = h + Σ tree[n-j]（j 为 lowbit(n) 以下的 2 的幂）
    const size_t n = m_lines.size();
    double v = h;
    const size_t low = n & (~n + 1);
    for (size_t j = 1; j < low; j <<= 1) v += m_fen[n - j];
    m_fen.push_back(v);
}

double ScrollModel::prefixOf(size_t i) const
{
    double s = 0;
    for (size_t k = std::min(i, m_lines.size()); k > 0; k -= (k & (~k + 1))) s += m_fen[k];
    return s;
}

void ScrollModel::fenwickAdd(size_t i, double delta)
{
    const size_t n = m_lines.size();
    for (size_t k = i + 1; k <= n; k += (k & (~k + 1))) m_fen[k] += delta;
}

void ScrollModel::rebuildFenwick()
{
    const size_t n = m_lines.size();
    m_fen.assign(n + 1, 0.0);
    for (size_t i = 1; i <= n; ++i) {
        m_fen[i] += m_h[i - 1];
        const size_t parent = i + (i & (~i + 1));
        if (parent <= n) m_fen[parent] += m_fen[i];
    }
}

size_t ScrollModel::indexAtY(double y) const
{
    const size_t n = m_lines.size();
    if (n == 0) return 0;
    if (y <= 0) return 0;
    size_t pos = 0;
    size_t step = 1;
    while ((step << 1) <= n) step <<= 1;
    double rem = y;
    for (; step > 0; step >>= 1) {
        const size_t nxt = pos + step;
        if (nxt <= n && m_fen[nxt] <= rem) {
            pos = nxt;
            rem -= m_fen[nxt];
        }
    }
    return std::min(pos, n - 1);
}

void ScrollModel::applyMeasured(size_t i, double h)
{
    if (h < 1.0) h = 1.0;
    const double old = m_h[i];
    if (!m_measured[i]) {
        m_measured[i] = 1;
        ++m_measuredCount;
        m_sumMeasuredH += h;
        m_sumMeasuredLines += static_cast<double>(std::max<uint32_t>(m_lines[i], 1));
    } else {
        m_sumMeasuredH += h - old;
    }
    m_h[i] = h;
    if (h != old) fenwickAdd(i, h - old);
}

double ScrollModel::ensureMeasured(size_t i)
{
    if (m_measured[i]) return m_h[i];
    const double h = m_measurer ? m_measurer(i) : estimate(i);
    applyMeasured(i, h);
    return m_h[i];
}

void ScrollModel::maybeRecalibrate()
{
    if (m_frozen || m_sumMeasuredLines <= 0) return;
    const double ratio = m_sumMeasuredH / m_sumMeasuredLines;
    if (ratio <= 0) return;
    if (std::fabs(ratio - m_lastRebuildPxPerLine) / m_lastRebuildPxPerLine < kRecalibrateRatio) return;
    m_pxPerLine = ratio;
    m_lastRebuildPxPerLine = ratio;
    for (size_t i = 0; i < m_lines.size(); ++i)
        if (!m_measured[i]) m_h[i] = estimate(i);
    rebuildFenwick();
}

double ScrollModel::topY() const
{
    if (m_lines.empty()) return 0;
    return prefixOf(m_anchorChunk) + m_anchorOffset;
}

double ScrollModel::maxTopY() const
{
    return std::max(0.0, total() - m_viewportH);
}

void ScrollModel::normalizeAnchor()
{
    if (m_lines.empty()) {
        m_anchorChunk = 0;
        m_anchorOffset = 0;
        return;
    }
    if (m_anchorChunk >= m_lines.size()) {
        m_anchorChunk = m_lines.size() - 1;
        m_anchorOffset = m_h[m_anchorChunk];
    }
    if (m_anchorOffset < 0) m_anchorOffset = 0;
    // 向前归一：offset 超出 chunk 高度则进入下一个 chunk
    while (m_anchorChunk + 1 < m_lines.size() && m_anchorOffset >= m_h[m_anchorChunk]) {
        m_anchorOffset -= m_h[m_anchorChunk];
        ++m_anchorChunk;
    }
    if (m_anchorOffset > m_h[m_anchorChunk]) m_anchorOffset = m_h[m_anchorChunk];
}

void ScrollModel::jumpTo(size_t chunk, double offset)
{
    if (m_lines.empty()) return;
    m_anchorChunk = std::min(chunk, m_lines.size() - 1);
    m_anchorOffset = std::max(0.0, offset);
    ensureMeasured(m_anchorChunk);
    normalizeAnchor();
}

void ScrollModel::scrollToStart()
{
    if (m_lines.empty()) return;
    m_anchorChunk = 0;
    m_anchorOffset = 0;
    ensureMeasured(0);
}

void ScrollModel::scrollToEnd()
{
    if (m_lines.empty()) return;
    double acc = 0;
    for (size_t i = m_lines.size(); i-- > 0;) {
        acc += ensureMeasured(i);
        if (acc >= m_viewportH) {
            m_anchorChunk = i;
            m_anchorOffset = acc - m_viewportH;
            normalizeAnchor();
            return;
        }
    }
    m_anchorChunk = 0;
    m_anchorOffset = 0;
}

void ScrollModel::setTopY(double y)
{
    if (m_lines.empty()) return;
    y = std::max(0.0, std::min(y, maxTopY()));
    const size_t idx = indexAtY(y);
    m_anchorChunk = idx;
    m_anchorOffset = y - prefixOf(idx);
    if (m_lines.size() - idx <= kTailMeasureWindow) {
        // 接近末尾：精算尾部保证 End 可达
        const double want = y;
        for (size_t i = m_lines.size(); i-- > idx;) ensureMeasured(i);
        const double mx = maxTopY();
        const double yy = std::min(want, mx);
        const size_t id2 = indexAtY(yy);
        m_anchorChunk = id2;
        m_anchorOffset = yy - prefixOf(id2);
    }
    normalizeAnchor();
}

void ScrollModel::scrollBy(double dy)
{
    if (m_lines.empty() || dy == 0) return;
    const double bigJump = std::max(4.0 * m_viewportH, 4000.0);
    if (std::fabs(dy) > bigJump) {
        setTopY(topY() + dy);
        return;
    }
    m_anchorOffset += dy;
    // 向后：逐个跨越 chunk，跨越的 chunk 必须已测量，保证偏移精确
    while (true) {
        const double h = ensureMeasured(m_anchorChunk);
        if (m_anchorOffset >= h && m_anchorChunk + 1 < m_lines.size()) {
            m_anchorOffset -= h;
            ++m_anchorChunk;
        } else {
            break;
        }
    }
    // 向前
    while (m_anchorOffset < 0 && m_anchorChunk > 0) {
        --m_anchorChunk;
        m_anchorOffset += ensureMeasured(m_anchorChunk);
    }
    if (m_anchorOffset < 0) m_anchorOffset = 0;
    // 末尾精算与夹紧
    if (dy > 0 && m_lines.size() - m_anchorChunk <= kTailMeasureWindow) {
        for (size_t i = m_lines.size(); i-- > m_anchorChunk;) ensureMeasured(i);
        if (topY() > maxTopY()) scrollToEnd();
    }
    normalizeAnchor();
}

void ScrollModel::invalidateAll()
{
    for (size_t i = 0; i < m_lines.size(); ++i) {
        m_measured[i] = 0;
        m_h[i] = estimate(i);
    }
    m_measuredCount = 0;
    m_sumMeasuredH = 0;
    m_sumMeasuredLines = 0;
    rebuildFenwick();
}

void ScrollModel::invalidate(size_t i)
{
    if (i >= m_lines.size() || !m_measured[i]) return;
    m_measured[i] = 0;
    --m_measuredCount;
    m_sumMeasuredH -= m_h[i];
    m_sumMeasuredLines -= static_cast<double>(std::max<uint32_t>(m_lines[i], 1));
    // 保持原高度直到重新测量，避免内容闪动
}

std::vector<ScrollModel::VisibleChunk> ScrollModel::visible(double bufferPx)
{
    std::vector<VisibleChunk> out;
    if (m_lines.empty()) return out;

    for (int pass = 0; pass < 3; ++pass) {
        out.clear();
        normalizeAnchor();
        ensureMeasured(m_anchorChunk);
        normalizeAnchor();
        double y = -m_anchorOffset;
        size_t i = m_anchorChunk;
        bool changed = false;
        while (i < m_lines.size() && y < m_viewportH + bufferPx) {
            const bool inView = y < m_viewportH;
            const double before = m_h[i];
            const double h = inView ? ensureMeasured(i) : m_h[i];
            if (inView && h != before && i == m_anchorChunk) changed = true;
            out.push_back({i, y, h});
            y += h;
            ++i;
        }
        // 文档末尾出现空白（实高比估高小）：回到底部对齐
        if (i >= m_lines.size() && y < m_viewportH && topY() > 0.5) {
            scrollToEnd();
            changed = true;
        }
        maybeRecalibrate();
        if (!changed) break;
    }
    return out;
}

}  // namespace viewer
