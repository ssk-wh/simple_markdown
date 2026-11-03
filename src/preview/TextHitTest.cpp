// src/preview/TextHitTest.cpp
//
// Spec: specs/模块-preview/12-选区与拖动.md
// Last synced: 2026-11-02
#include "TextHitTest.h"

#include <QFontMetricsF>
#include <QTextBoundaryFinder>
#include <limits>

QPair<int,int> TextHitTest::findWordBoundaryFor(const QString& text, int idx)
{
    if (idx < 0 || idx >= text.length()) return {-1, -1};

    // 用 QTextBoundaryFinder::Word 做 ICU 风格分词，与 QTextEdit / 浏览器一致。
    // 旧实现用 QChar::isLetterOrNumber 向两侧扩展，但该谓词对 CJK（汉字/假名/谚文）
    // 也返回 true——一整段中文会被当成一个"词"整段吞掉，用户感知"双击选不到
    // 我点的那个词"（实际选了整段）。
    QTextBoundaryFinder finder(QTextBoundaryFinder::Word, text);

    // 向左找词首（StartOfItem）
    finder.setPosition(idx);
    if (!(finder.boundaryReasons() & QTextBoundaryFinder::StartOfItem)) {
        while (finder.toPreviousBoundary() >= 0) {
            if (finder.boundaryReasons() & QTextBoundaryFinder::StartOfItem) break;
        }
    }
    int wordStart = qMax(0, finder.position());

    // 从词首向右找词尾（EndOfItem）
    finder.setPosition(wordStart);
    while (finder.toNextBoundary() >= 0) {
        if (finder.boundaryReasons() & QTextBoundaryFinder::EndOfItem) break;
    }
    int wordEnd = finder.position();
    if (wordEnd < 0) wordEnd = text.length();

    // 退化路径 A：boundary 落在非词字符串上（如词与词之间的空格 / 标点段），
    // BoundaryFinder 仍会切出一段"非词项"——用户期望的是只选当前字符。
    // 判定：点击位置不在 [wordStart, wordEnd) 内 → 区间不含点击 → 退化
    if (wordStart < 0 || wordEnd <= wordStart || idx < wordStart || idx >= wordEnd) {
        return {idx, qMin(idx + 1, text.length())};
    }

    // 退化路径 B：Qt 5.12 的 QTextBoundaryFinder::Word 不对纯 CJK 字符串切分
    // 词边界，整段中文被当成一个"词"。若区间 = 全文且点击字符为 CJK 脚本，
    // 回退到单字符选中，避免"双击选不到我点的那个词"。
    if (wordEnd - wordStart == text.length() && text.length() > 1) {
        const auto sc = text.at(idx).script();
        if (sc == QChar::Script_Han || sc == QChar::Script_Hiragana ||
            sc == QChar::Script_Katakana || sc == QChar::Script_Hangul) {
            return {idx, idx + 1};
        }
    }

    return {wordStart, wordEnd};
}

// 在段内使用字体度量逐字精确定位字符索引
// device 参数必须传入，以确保高 DPI 下的度量与绘制时一致
static int hitTestSegment(const TextSegment& seg, qreal relX, QPaintDevice* device)
{
    if (seg.text.isEmpty()) return seg.charStart;
    QFontMetricsF fm(seg.font, device);  // 使用 device 参数确保度量一致
    for (int i = 0; i < seg.text.length(); ++i) {
        qreal w = fm.horizontalAdvance(seg.text.left(i + 1));
        if (relX < w) {
            qreal prevW = (i > 0) ? fm.horizontalAdvance(seg.text.left(i)) : 0;
            return seg.charStart + ((relX - prevW < w - relX) ? i : i + 1);
        }
    }
    return seg.charStart + seg.charLen;
}

int TextHitTest::textIndexForSegments(const QVector<TextSegment>& segments,
                                        const QPointF& point, QPaintDevice* device)
{
    if (segments.isEmpty())
        return 0;

    // [Spec 模块-preview/12 INV-1] 双层 closest：
    //  - inRow: 鼠标 y 落在 segment rect 垂直范围内的候选（同一视觉行）——优先取
    //    dx 最近的；这一层防止用户在表格"行右侧空白"拖拽时 snap 跳到上下行
    //  - any:   2D 欧氏距离最近候选，作为 fallback（鼠标 y 落在行间空白时使用）
    int inRowClosest = -1;
    qreal inRowMinDx = std::numeric_limits<qreal>::max();
    int anyClosest = 0;
    qreal anyMinDist = std::numeric_limits<qreal>::max();

    for (const auto& seg : segments) {
        if (seg.rect.contains(point)) {
            return hitTestSegment(seg, point.x() - seg.rect.x(), device);
        }

        // 候选 1：同一视觉行优先（鼠标 y 在 seg 垂直范围内）——表格右侧空白扩散修复
        const bool inSameRow =
            point.y() >= seg.rect.top() && point.y() <= seg.rect.bottom();
        if (inSameRow) {
            qreal dx = 0;
            if (point.x() < seg.rect.left())       dx = seg.rect.left() - point.x();
            else if (point.x() > seg.rect.right()) dx = point.x() - seg.rect.right();

            if (dx < inRowMinDx) {
                inRowMinDx = dx;
                if (point.x() >= seg.rect.right())
                    inRowClosest = seg.charStart + seg.charLen;
                else if (point.x() <= seg.rect.left())
                    inRowClosest = seg.charStart;
                else
                    inRowClosest = hitTestSegment(seg, point.x() - seg.rect.x(), device);
            }
        }

        // 候选 2：2D 距离 fallback
        qreal dx = 0, dy = 0;
        if (point.x() < seg.rect.left())       dx = seg.rect.left() - point.x();
        else if (point.x() > seg.rect.right()) dx = point.x() - seg.rect.right();
        if (point.y() < seg.rect.top())        dy = seg.rect.top() - point.y();
        else if (point.y() > seg.rect.bottom()) dy = point.y() - seg.rect.bottom();
        qreal dist = dy * dy + dx * dx;

        if (dist < anyMinDist) {
            anyMinDist = dist;
            if (point.x() >= seg.rect.right())
                anyClosest = seg.charStart + seg.charLen;
            else if (point.x() <= seg.rect.left())
                anyClosest = seg.charStart;
            else
                anyClosest = hitTestSegment(seg, point.x() - seg.rect.x(), device);
        }
    }

    // 优先同行候选——这是表格行右侧空白选区修复的关键：表格行 X 内拖到右侧空白时，
    // 行 X 的 segments 必然在 inRow 候选集中，dx 最近通常就是该行最右 segment 末尾，
    // 不会再 snap 到上下行 segment
    return (inRowClosest >= 0) ? inRowClosest : anyClosest;
}
