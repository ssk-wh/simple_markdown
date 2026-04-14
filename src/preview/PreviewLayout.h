#pragma once

#include <QFont>
#include <QColor>
#include <QRectF>
#include <QString>
#include <vector>
#include <memory>

class QPaintDevice;

#include "Theme.h"

class AstNode;

struct InlineRun {
    QString text;
    QFont font;
    QColor color = Qt::black;
    QColor bgColor = Qt::transparent;
    QString linkUrl;
    bool isStrikethrough = false;
};

struct LayoutBlock {
    enum Type {
        Document, Paragraph, Heading, CodeBlock, BlockQuote,
        List, ListItem, Table, TableRow, TableCell,
        Image, ThematicBreak, HtmlBlock
    };
    Type type = Document;
    QRectF bounds;
    int sourceStartLine = -1;
    int sourceEndLine = -1;

    // Paragraph/Heading: inline content
    std::vector<InlineRun> inlineRuns;

    // CodeBlock
    QString codeText;
    QString language;

    // Image
    QString imageUrl;
    bool imageLoaded = false;

    // Heading
    int headingLevel = 0;

    // List
    bool ordered = false;
    int listStart = 1;

    // Table
    std::vector<qreal> columnWidths;
    std::vector<int> columnAligns;

    std::vector<LayoutBlock> children;
};

class PreviewLayout {
public:
    PreviewLayout();
    ~PreviewLayout();

    void setViewportWidth(qreal width);
    void setFont(const QFont& baseFont);
    bool updateMetrics(QPaintDevice* device);  // 返回 true 表示度量有变化
    void setTheme(const Theme& theme);
    void buildFromAst(const std::shared_ptr<AstNode>& root);
    qreal totalHeight() const;
    const LayoutBlock& rootBlock() const;

    // Spec 模块-preview/02-布局引擎 INV-9: 供 PreviewPainter 读取字体，禁止构造
    const QFont& baseFont() const { return m_baseFont; }
    const QFont& monoFont() const { return m_monoFont; }
    qreal sourceLineToY(int sourceLine) const;
    int yToSourceLine(qreal y) const;

private:
    LayoutBlock layoutBlock(const AstNode* node, qreal maxWidth);
    void collectInlineRuns(const AstNode* node, std::vector<InlineRun>& runs,
                           QFont currentFont, QColor currentColor);
    qreal estimateParagraphHeight(const std::vector<InlineRun>& runs, qreal maxWidth) const;
    void collectSourceMappings(const LayoutBlock& block, qreal offsetY,
                               std::vector<std::pair<int, qreal>>& mappings) const;

    LayoutBlock m_root;
    QFont m_baseFont;
    QFont m_monoFont;
    Theme m_theme;
    qreal m_viewportWidth = 600.0;
    qreal m_lineHeight = 24.0;
    qreal m_codeLineHeight = 20.0;
    QPaintDevice* m_device = nullptr;  // [高 DPI 修复] 用于高度估计中的字体度量
};
