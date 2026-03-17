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

    // 剪贴板
    void cut();
    void copy();
    void paste();

    // TextPosition <-> offset 转换
    int posToOffset(TextPosition pos);
    TextPosition offsetToPos(int offset);

    // 更新光标位置并通知 Widget
    void moveCursorTo(TextPosition pos, bool select);
};
