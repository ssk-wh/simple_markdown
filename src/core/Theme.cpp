#include "Theme.h"

Theme Theme::light() {
    Theme t;
    t.name = "Light";
    // All defaults are already light theme values
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

    return t;
}
