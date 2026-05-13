#pragma once
#include <QKeyEvent>
#include "Selection.h"

class EditorWidget;
class Document;
class EditorLayout;

class EditorInput {
public:
    explicit EditorInput(EditorWidget* editor);

    bool keyPressEvent(QKeyEvent* event);

    // IME 提交文本
    void insertText(const QString& text);

    // [plan B7/B8 2026-05-12] 图片落地 helper（EditorWidget::insertImageMarkdown 复用）
    QString copyImageFileToImagesDir(const QString& srcPath);

private:
    EditorWidget* m_editor;

    // 辅助
    Document* doc();
    Selection& sel();
    EditorLayout* layout();
    void deleteForward();
    void deleteBackward();
    void insertNewLine();

    // 光标移动
    void moveLeft(bool select);
    void moveRight(bool select);
    void moveUp(bool select);
    void moveDown(bool select);
    void moveToLineStart(bool select);
    void moveToLineEnd(bool select);
    void moveToDocStart(bool select);
    void moveToDocEnd(bool select);
    void pageUp(bool select);
    void pageDown(bool select);

    // 快捷键
    void undo();
    void redo();
    void selectAll();
    void indent();
    void unindent();

    // Markdown 格式化
    void wrapSelection(const QString& before, const QString& after);

    // 剪贴板
    void cut();
    void copy();
    void paste();

    // [plan B7 2026-05-12] 把剪贴板里的图片保存到当前文档同级 ./images/ 并返回
    // 相对路径（如 "images/doc-20260513-091530.png"）。失败/未保存文档返回空。
    QString saveClipboardImageToImagesDir();

    // TextPosition <-> offset 转换
    int posToOffset(TextPosition pos);
    TextPosition offsetToPos(int offset);

    // 更新光标位置并通知 Widget
    void moveCursorTo(TextPosition pos, bool select);
};
