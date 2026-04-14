// Spec: specs/模块-preview/10-Frontmatter渲染.md §4.4 §5.4 §5.5
// Spec: specs/横切关注点/30-主题系统.md （新增 accentColor / frontmatter* 字段）
// Last synced: 2026-04-14
#include "Theme.h"

#include <QGuiApplication>
#include <QPalette>

namespace {

// Spec §5.4：RGB 线性混合（不是 alpha 合成），避免与选区/marking 叠加出现双重混合
QColor blendColor(const QColor& accent, const QColor& bg, qreal accentRatio)
{
    const qreal r = accent.redF()   * accentRatio + bg.redF()   * (1.0 - accentRatio);
    const qreal g = accent.greenF() * accentRatio + bg.greenF() * (1.0 - accentRatio);
    const qreal b = accent.blueF()  * accentRatio + bg.blueF()  * (1.0 - accentRatio);
    return QColor::fromRgbF(qBound(0.0, r, 1.0),
                            qBound(0.0, g, 1.0),
                            qBound(0.0, b, 1.0));
}

// Spec §5.5：从系统 palette 取强调色；palette 全灰时 fallback #0078D4
QColor resolveSystemAccent()
{
    const QColor kFallback("#0078D4");
    if (!QGuiApplication::instance())
        return kFallback;
    const QPalette pal = QGuiApplication::palette();
    const QColor hi = pal.color(QPalette::Highlight);
    if (!hi.isValid())
        return kFallback;
    // 判断是否"全灰"：R=G=B 且饱和度极低
    if (hi.red() == hi.green() && hi.green() == hi.blue())
        return kFallback;
    if (hi.saturation() < 20)
        return kFallback;
    return hi;
}

void applyFrontmatterColors(Theme& t)
{
    // Spec §5.4/§8.5：背景用 accent 与 previewBg 线性混合。Spec 建议 0.5/0.7，
    // 实测深色主题下 0.5 视觉过重，浅色主题下 0.5 可接受。按 §8.5 建议做主题差异化。
    t.accentColor = resolveSystemAccent();
    if (t.isDark) {
        t.frontmatterBackground = blendColor(t.accentColor, t.previewBg, 0.22);
        t.frontmatterBorder     = blendColor(t.accentColor, t.previewBg, 0.60);
    } else {
        t.frontmatterBackground = blendColor(t.accentColor, t.previewBg, 0.12);
        t.frontmatterBorder     = blendColor(t.accentColor, t.previewBg, 0.50);
    }
    // Key 偏 accent：accent 与 previewFg 线性混合，保持可读
    t.frontmatterKeyForeground   = blendColor(t.accentColor, t.previewFg, 0.65);
    // Value 与 codeFg 一致
    t.frontmatterValueForeground = t.previewCodeFg;
}

} // namespace

Theme Theme::light() {
    Theme t;
    t.name = "Light";
    // All defaults are already light theme values
    applyFrontmatterColors(t);
    return t;
}

Theme Theme::dark() {
    Theme t;
    t.name = "Dark";
    t.isDark = true;

    // 编辑器颜色
    t.editorBg = QColor("#1E1E1E");
    t.editorFg = QColor("#D4D4D4");
    t.editorCurrentLine = QColor("#2A2A2A");
    t.editorSelection = QColor("#264F78");
    t.editorLineNumber = QColor("#858585");
    t.editorLineNumberActive = QColor("#C6C6C6");
    t.editorGutterBg = QColor("#1E1E1E");
    t.editorCursor = QColor("#D4D4D4");
    t.editorSearchMatch = QColor(218, 165, 32, 220);   // 深金黄（alpha 220），深色背景上清晰可见
    t.editorSearchMatchCurrent = QColor(255, 140, 0, 240);  // 当前匹配项（饱和橙色）
    t.editorGutterLine = QColor("#333333");
    t.editorPreeditBg = QColor(80, 80, 50);

    // 语法高亮颜色
    t.syntaxHeading = QColor("#569CD6");
    t.syntaxCode = QColor("#CE9178");
    t.syntaxCodeBg = QColor("#2D2D2D");
    t.syntaxCodeBlock = QColor("#6A9955");
    t.syntaxCodeBlockBg = QColor("#2D2D2D");
    t.syntaxLink = QColor("#4FC3F7");
    t.syntaxList = QColor("#C586C0");
    t.syntaxBlockQuote = QColor("#808080");
    t.syntaxFence = QColor("#808080");
    t.syntaxFenceBg = QColor("#2D2D2D");

    // 预览颜色
    t.previewBg = QColor("#1E1E1E");
    t.previewFg = QColor("#D4D4D4");
    t.previewHeading = QColor("#FFFFFF");
    t.previewLink = QColor("#4FC3F7");
    t.previewCodeBg = QColor("#2D2D2D");
    t.previewCodeFg = QColor("#CE9178");
    t.previewCodeBorder = QColor("#444444");
    t.previewBlockQuoteBorder = QColor("#444444");
    t.previewBlockQuoteBg = QColor("#252525");
    t.previewTableBorder = QColor("#444444");
    t.previewTableHeaderBg = QColor("#2D2D2D");
    t.previewHr = QColor("#444444");
    t.previewHeadingSeparator = QColor("#444444");
    t.previewImagePlaceholderBg = QColor("#2D2D2D");
    t.previewImagePlaceholderBorder = QColor("#444444");
    t.previewImagePlaceholderText = QColor("#808080");
    t.previewImageErrorBg = QColor("#3D2020");
    t.previewImageErrorBorder = QColor("#B71C1C");
    t.previewImageErrorText = QColor("#EF9A9A");
    t.previewImageInfoText = QColor("#A0A0A0");
    t.previewHighlight = QColor(255, 167, 38, 120);        // 橙黄色
    t.previewHighlightToc = QColor(180, 120, 0, 150);      // 深金黄

    applyFrontmatterColors(t);
    return t;
}
