#pragma once
#include <QColor>
#include <QString>

struct Theme {
    QString name;
    bool isDark = false;

    // 编辑器颜色
    QColor editorBg = QColor("#FFFFFF");
    QColor editorFg = QColor("#333333");
    QColor editorCurrentLine = QColor("#F5F5F5");
    QColor editorSelection = QColor("#B5D5FF");
    QColor editorLineNumber = QColor("#999999");
    QColor editorLineNumberActive = QColor("#333333");
    QColor editorGutterBg = QColor("#F0F0F0");
    QColor editorCursor = QColor("#333333");
    QColor editorSearchMatch = QColor(255, 235, 59, 220);  // 亮黄色（alpha 220 保证明显）
    QColor editorSearchMatchCurrent = QColor(255, 140, 0, 240);  // 当前匹配（饱和橙色，更明显）
    QColor editorGutterLine = QColor("#E0E0E0");
    QColor editorPreeditBg = QColor(255, 255, 200);

    // 语法高亮颜色
    QColor syntaxHeading = QColor("#1A237E");
    QColor syntaxCode = QColor("#C62828");
    QColor syntaxCodeBg = QColor("#F5F5F5");
    QColor syntaxCodeBlock = QColor("#2E7D32");
    QColor syntaxCodeBlockBg = QColor("#F8F8F8");
    QColor syntaxLink = QColor("#0366D6");
    QColor syntaxList = QColor("#6A1B9A");
    QColor syntaxBlockQuote = QColor("#757575");
    QColor syntaxFence = QColor("#999999");
    QColor syntaxFenceBg = QColor("#F0F0F0");

    // 预览颜色
    QColor previewBg = QColor("#FFFFFF");
    QColor previewFg = QColor("#333333");
    QColor previewHeading = QColor("#1A1A1A");
    QColor previewLink = QColor("#0366D6");
    QColor previewCodeBg = QColor("#F6F8FA");
    QColor previewCodeFg = QColor("#333333");
    QColor previewCodeBorder = QColor("#E1E4E8");
    QColor previewBlockQuoteBorder = QColor("#DFE2E5");
    QColor previewBlockQuoteBg = QColor("#F8F8F8");
    QColor previewTableBorder = QColor("#DFE2E5");
    QColor previewTableHeaderBg = QColor("#F6F8FA");
    QColor previewHr = QColor("#E0E0E0");
    QColor previewHeadingSeparator = QColor("#EAECEF");
    QColor previewImagePlaceholderBg = QColor("#F0F0F0");
    QColor previewImagePlaceholderBorder = QColor("#CCCCCC");
    QColor previewImagePlaceholderText = QColor("#999999");
    QColor previewImageErrorBg = QColor("#FFF0F0");
    QColor previewImageErrorBorder = QColor("#E57373");
    QColor previewImageErrorText = QColor("#C62828");
    QColor previewImageInfoText = QColor("#666666");
    QColor previewHighlight = QColor(255, 235, 59, 180);     // 标记高亮（浅色）
    QColor previewHighlightToc = QColor(255, 235, 59, 120);  // TOC标记（浅色）

    static Theme light();
    static Theme dark();
};
