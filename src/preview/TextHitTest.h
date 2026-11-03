// src/preview/TextHitTest.h
//
// Spec: specs/模块-preview/12-选区与拖动.md（INV-1 选区终点 snap、INV-4 双击选词）
// 由旧 ViewerWidget 的两个 testable seam 抽出（ViewerWidget 已随 2.0.0 移除），
// 现供 ViewerWidget 与单元测试直接使用。
// Last synced: 2026-11-02
#pragma once

#include <QPair>
#include <QPointF>
#include <QString>
#include <QVector>

#include "PreviewPainter.h"

class QPaintDevice;

class TextHitTest {
public:
    // [INV-4] 双击选词的 Word 边界分词：返回 [wordStart, wordEnd)；idx 越界返回 {-1,-1}
    static QPair<int,int> findWordBoundaryFor(const QString& text, int idx);

    // [INV-1] 选区终点 snap：把鼠标 point 映射到 segments 中的字符索引（双层 closest）
    static int textIndexForSegments(const QVector<TextSegment>& segments,
                                    const QPointF& point, QPaintDevice* device);
};
