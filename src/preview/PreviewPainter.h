#pragma once

#include "PreviewLayout.h"
#include "Theme.h"

#include <QPainter>
#include <QVector>
#include <QRectF>
#include <QPair>

class ImageCache;

struct TextSegment {
    QRectF rect;       // 屏幕坐标（相对于 viewport，已减 scrollY）
    int charStart;     // 在纯文本中的起始字符索引
    int charLen;       // 字符长度
    QString text;      // 段内文本（用于逐字精确定位）
    QFont font;        // 段所用字体
    QString linkUrl;   // [Spec 模块-preview/09] 链接 URL，空串表示非链接
};

// [测试模式] 行内元素信息
#ifdef ENABLE_TEST_MODE
struct InlineRunInfo {
    QString text;
    QString fontFamily;
    double fontSize = 0;
    int fontWeight = 0;       // 400=normal, 700=bold
    QString color;            // "#rrggbb"
    QString bgColor;          // "#rrggbb" or empty
    bool isLink = false;
    bool isStrikethrough = false;
};

// [测试模式] 渲染块信息，用于自动化测试验证
struct BlockInfo {
    QString type;                    // "heading", "paragraph", "code_block", etc.
    int x, y, width, height;        // 屏幕坐标（已减 scrollY）
    QString contentText;             // 块文本内容（前200字符）
    int sourceStart = -1;            // 源 markdown 起始行
    int sourceEnd = -1;              // 源 markdown 结束行
    int headingLevel = 0;            // 标题级别（仅标题块）
    QString fontFamily;              // 主字体名称
    double fontSize = 0;             // 主字体大小 pt
    int fontWeight = 0;              // 主字体粗细
    bool ordered = false;            // 是否有序列表
    int listStart = 1;               // 列表起始编号
    int bulletX = -1, bulletY = -1;  // 序号/圆点绘制位置（仅 list_item）
    int bulletWidth = 0;             // 序号/圆点宽度
    QString codeLanguage;            // 代码块语言
    QVector<InlineRunInfo> inlineRuns; // 行内元素
    QVector<BlockInfo> children;     // 子块（递归）
};
#endif

class PreviewPainter {
public:
    PreviewPainter();
    ~PreviewPainter();

    void setTheme(const Theme& theme);
    const Theme& theme() const { return m_theme; }
    void setImageCache(ImageCache* cache);

    // Spec 模块-preview/03 INV-8/9: 绘制侧禁止构造 QFont，必须从 layout 取字体
    void setLayout(const PreviewLayout* layout) { m_layout = layout; }

    void paint(QPainter* painter, const LayoutBlock& root,
               qreal scrollY, qreal viewportHeight, qreal viewportWidth);

    const QVector<TextSegment>& textSegments() const { return m_textSegments; }

    // 选区绘制
    void setSelection(int selStart, int selEnd);

    // 标记高亮
    void setHighlights(const QVector<QPair<int,int>>& highlights);

    // TOC 跳转目标行高亮
    void setTargetLineHighlight(int sourceLine, qreal opacity);

    // [测试模式] 获取记录的块信息，并输出到 JSON 文件
#ifdef ENABLE_TEST_MODE
    void saveBlocksToJson(int viewportWidth, int viewportHeight) const;
    const BlockInfo& rootBlockInfo() const { return m_rootBlockInfo; }
#endif

private:
    void paintBlock(QPainter* p, const LayoutBlock& block,
                    qreal offsetX, qreal offsetY,
                    qreal scrollY, qreal viewportHeight, qreal viewportWidth);
    void paintInlineRuns(QPainter* p, const LayoutBlock& block,
                         qreal x, qreal y, qreal maxWidth);
    // Spec: specs/模块-preview/10-Frontmatter渲染.md §4.6
    void paintFrontmatter(QPainter* p, const LayoutBlock& block,
                          qreal absX, qreal absY);
    void recordSegment(const QRectF& rect, int charStart, int charLen,
                       const QString& text, const QFont& font,
                       const QString& linkUrl = QString());
    void countBlockChars(const LayoutBlock& block);

    Theme m_theme;
    const PreviewLayout* m_layout = nullptr;  // Spec 模块-preview/03 INV-9
    ImageCache* m_imageCache = nullptr;
    QVector<TextSegment> m_textSegments;
    int m_charCounter = 0;  // 绘制期间的字符计数器
    int m_selStart = -1;
    int m_selEnd = -1;
    QVector<QPair<int,int>> m_highlights;  // 标记高亮范围 (start, end)
    int m_targetSourceLine = -1;  // TOC 跳转目标行
    qreal m_targetHighlightOpacity = 0.0;  // 目标行高亮透明度

#ifdef ENABLE_TEST_MODE
    mutable BlockInfo m_rootBlockInfo;  // 递归渲染树（用于测试验证）
    static BlockInfo buildBlockInfo(const LayoutBlock& block, qreal drawX, qreal drawY);
    static QJsonObject blockInfoToJson(const BlockInfo& info);
#endif
};
