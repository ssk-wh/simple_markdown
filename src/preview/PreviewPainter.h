#pragma once

#include "PreviewLayout.h"
#include "Theme.h"

#include <QPainter>

class PreviewPainter {
public:
    PreviewPainter();
    ~PreviewPainter();

    void setTheme(const Theme& theme);
    const Theme& theme() const { return m_theme; }

    void paint(QPainter* painter, const LayoutBlock& root,
               qreal scrollY, qreal viewportHeight, qreal viewportWidth);

private:
    void paintBlock(QPainter* p, const LayoutBlock& block,
                    qreal offsetX, qreal offsetY,
                    qreal scrollY, qreal viewportHeight, qreal viewportWidth);
    void paintInlineRuns(QPainter* p, const LayoutBlock& block,
                         qreal x, qreal y, qreal maxWidth);

    Theme m_theme;
};
