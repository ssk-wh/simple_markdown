#pragma once

#include <QFont>
#include <QFontMetricsF>
#include <QColor>
#include <QRectF>
#include <QString>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

class QPaintDevice;

#include "Theme.h"

class AstNode;
class ImageCache;

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
        Image, ThematicBreak, HtmlBlock,
        Frontmatter  // Spec: specs/模块-preview/10-Frontmatter渲染.md §4.3
    };
    Type type = Document;
    QRectF bounds;
    int sourceStartLine = -1;
    int sourceEndLine = -1;

    // 段落/标题：行内内容
    std::vector<InlineRun> inlineRuns;

    // 代码块
    QString codeText;
    QString language;

    // 图片
    QString imageUrl;
    bool imageLoaded = false;

    // 标题
    int headingLevel = 0;

    // 列表
    bool ordered = false;
    int listStart = 1;

    // 表格
    std::vector<qreal> columnWidths;
    std::vector<int> columnAligns;

    // Frontmatter (Spec §4.3)
    std::vector<std::pair<QString, QString>> frontmatterEntries;
    qreal frontmatterKeyColumnWidth = 0;  // 由 layout 阶段写入（INV-10）
    QString frontmatterRawText;            // 原始 YAML，用于 INV-13 复制

    std::vector<LayoutBlock> children;
};

class PreviewLayout {
public:
    PreviewLayout();
    ~PreviewLayout();

    void setViewportWidth(qreal width);
    void setImageCache(ImageCache* cache);
    void setFont(const QFont& baseFont);
    // [Spec 模块-preview/02 INV-13] 正文行高乘数（默认 1.5，作用范围仅正文段落 / List / Table）
    void setLineSpacingFactor(qreal factor);
    qreal lineSpacingFactor() const { return m_lineSpacingFactor; }
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

    // Spec: specs/模块-preview/10-Frontmatter渲染.md §4.5
    qreal codeLineHeight() const { return m_codeLineHeight; }

private:
    LayoutBlock layoutBlock(const AstNode* node, qreal maxWidth);
    LayoutBlock layoutFrontmatter(const AstNode* node, qreal maxWidth);
    void collectInlineRuns(const AstNode* node, std::vector<InlineRun>& runs,
                           QFont currentFont, QColor currentColor);
    qreal estimateParagraphHeight(const std::vector<InlineRun>& runs, qreal maxWidth) const;
    void collectSourceMappings(const LayoutBlock& block, qreal offsetY,
                               std::vector<std::pair<int, qreal>>& mappings) const;

    // 缓存 QFontMetricsF，避免每个 InlineRun 重复构造（性能优化方案 B）
    const QFontMetricsF& cachedFontMetrics(const QFont& font) const;
    void clearFontMetricsCache();

    LayoutBlock m_root;
    QFont m_baseFont;
    QFont m_monoFont;
    Theme m_theme;
    qreal m_viewportWidth = 600.0;
    // [Spec 模块-preview/02 INV-13] 默认 1.5：与 MainWindow::loadSettings 的
    // "view/lineSpacing" 默认值保持一致，避免新构造的 PreviewLayout 在未收到
    // setLineSpacingFactor 推送前显示出比用户预期更紧的行高
    qreal m_lineSpacingFactor = 1.5;
    qreal m_lineHeight = 24.0;
    qreal m_codeLineHeight = 20.0;
    QPaintDevice* m_device = nullptr;  // [高 DPI 修复] 用于高度估计中的字体度量
    ImageCache* m_imageCache = nullptr;
    // [Spec 模块-preview/02 INV-12] cache key 用字体属性元组拼成的字符串，
    // 而非 qHash(QFont)：Qt 5.12 的 qHash 在不同 pointSize 下也会冲突，
    // 会让列表 bold 段落取到 H1 的度量，导致首次渲染时列表项之间多出半行空白
    // （缩放后才消失）。Qt 5.12 没有给 QString 提供 std::hash 特化，所以
    // unordered_map 用 std::string 做 key。
    mutable std::unordered_map<std::string, QFontMetricsF> m_fontMetricsCache;
};
