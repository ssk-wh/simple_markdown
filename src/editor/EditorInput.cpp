#include "EditorInput.h"
#include "EditorWidget.h"
#include "EditorLayout.h"
#include "Document.h"

#include <QScrollBar>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QImage>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QRegularExpression>
#include <algorithm>

EditorInput::EditorInput(EditorWidget* editor)
    : m_editor(editor)
{
}

Document* EditorInput::doc()
{
    return m_editor->document();
}

Selection& EditorInput::sel()
{
    return doc()->selection();
}

EditorLayout* EditorInput::layout()
{
    return m_editor->editorLayout();
}

int EditorInput::posToOffset(TextPosition pos)
{
    return doc()->lineToOffset(pos.line) + pos.column;
}

TextPosition EditorInput::offsetToPos(int offset)
{
    int line = doc()->offsetToLine(offset);
    int col = offset - doc()->lineToOffset(line);
    return {line, col};
}

bool EditorInput::keyPressEvent(QKeyEvent* event)
{
    bool shift = event->modifiers() & Qt::ShiftModifier;
    bool ctrl = event->modifiers() & Qt::ControlModifier;

    switch (event->key()) {
    case Qt::Key_Left:      moveLeft(shift); return true;
    case Qt::Key_Right:     moveRight(shift); return true;
    case Qt::Key_Up:        moveUp(shift); return true;
    case Qt::Key_Down:      moveDown(shift); return true;
    case Qt::Key_Home:
        if (ctrl) moveToDocStart(shift); else moveToLineStart(shift);
        return true;
    case Qt::Key_End:
        if (ctrl) moveToDocEnd(shift); else moveToLineEnd(shift);
        return true;
    case Qt::Key_PageUp:    pageUp(shift); return true;
    case Qt::Key_PageDown:  pageDown(shift); return true;
    case Qt::Key_Backspace: deleteBackward(); return true;
    case Qt::Key_Delete:    deleteForward(); return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:     insertNewLine(); return true;
    case Qt::Key_Tab:
        if (shift) unindent(); else indent();
        return true;
    default: break;
    }

    if (ctrl) {
        switch (event->key()) {
        case Qt::Key_Z: undo(); return true;
        case Qt::Key_Y: redo(); return true;
        case Qt::Key_A: selectAll(); return true;
        case Qt::Key_C: copy(); return true;
        case Qt::Key_X: cut(); return true;
        case Qt::Key_V: paste(); return true;
        case Qt::Key_F: m_editor->showSearchBar(); return true;
        case Qt::Key_H: m_editor->showReplaceBar(); return true;
        case Qt::Key_B: wrapSelection("**", "**"); return true;   // 加粗
        case Qt::Key_I: wrapSelection("*", "*"); return true;     // 斜体
        case Qt::Key_K: wrapSelection("[", "](url)"); return true; // 链接
        case Qt::Key_D: wrapSelection("~~", "~~"); return true;   // 删除线
        case Qt::Key_E: wrapSelection("`", "`"); return true;     // 行内代码
        default: break;
        }
    }

    // 普通文字输入
    QString text = event->text();
    if (!text.isEmpty() && text[0].isPrint()) {
        insertText(text);
        return true;
    }

    return false;
}

void EditorInput::insertText(const QString& text)
{
    if (sel().hasSelection()) {
        TextPosition start = sel().range().start();
        TextPosition end = sel().range().end();
        int startOff = posToOffset(start);
        int endOff = posToOffset(end);
        doc()->remove(startOff, endOff - startOff);
        sel().setCursorPosition(start);
    }

    int offset = posToOffset(sel().cursorPosition());
    doc()->insert(offset, text);

    TextPosition newPos = offsetToPos(offset + text.length());
    sel().setCursorPosition(newPos);
    sel().resetPreferredColumn();
}

void EditorInput::deleteBackward()
{
    if (sel().hasSelection()) {
        TextPosition start = sel().range().start();
        TextPosition end = sel().range().end();
        int startOff = posToOffset(start);
        int endOff = posToOffset(end);
        doc()->remove(startOff, endOff - startOff);
        sel().setCursorPosition(start);
    } else {
        int offset = posToOffset(sel().cursorPosition());
        if (offset > 0) {
            doc()->remove(offset - 1, 1);
            sel().setCursorPosition(offsetToPos(offset - 1));
        }
    }
    sel().resetPreferredColumn();
}

void EditorInput::deleteForward()
{
    if (sel().hasSelection()) {
        TextPosition start = sel().range().start();
        TextPosition end = sel().range().end();
        int startOff = posToOffset(start);
        int endOff = posToOffset(end);
        doc()->remove(startOff, endOff - startOff);
        sel().setCursorPosition(start);
    } else {
        int offset = posToOffset(sel().cursorPosition());
        if (offset < doc()->length()) {
            doc()->remove(offset, 1);
        }
    }
    sel().resetPreferredColumn();
}

void EditorInput::insertNewLine()
{
    // 自动缩进：获取当前行前导空白
    TextPosition pos = sel().cursorPosition();
    QString currentLine = doc()->lineText(pos.line);
    QString indentStr;
    for (QChar c : currentLine) {
        if (c == ' ' || c == '\t') indentStr += c;
        else break;
    }
    insertText(QStringLiteral("\n") + indentStr);
}

void EditorInput::moveLeft(bool select)
{
    TextPosition pos = sel().cursorPosition();
    if (pos.column > 0) {
        pos.column--;
    } else if (pos.line > 0) {
        pos.line--;
        pos.column = doc()->lineText(pos.line).length();
    }
    moveCursorTo(pos, select);
}

void EditorInput::moveRight(bool select)
{
    TextPosition pos = sel().cursorPosition();
    int lineLen = doc()->lineText(pos.line).length();
    if (pos.column < lineLen) {
        pos.column++;
    } else if (pos.line < doc()->lineCount() - 1) {
        pos.line++;
        pos.column = 0;
    }
    moveCursorTo(pos, select);
}

void EditorInput::moveUp(bool select)
{
    TextPosition pos = sel().cursorPosition();
    if (pos.line > 0) {
        pos.line--;
        int pref = sel().preferredColumn();
        if (pref < 0) pref = pos.column;
        pos.column = qMin(pref, doc()->lineText(pos.line).length());
        sel().setPreferredColumn(pref);
        moveCursorTo(pos, select);
        return;
    }
    pos.column = 0;
    moveCursorTo(pos, select);
}

void EditorInput::moveDown(bool select)
{
    TextPosition pos = sel().cursorPosition();
    if (pos.line < doc()->lineCount() - 1) {
        pos.line++;
        int pref = sel().preferredColumn();
        if (pref < 0) pref = pos.column;
        pos.column = qMin(pref, doc()->lineText(pos.line).length());
        sel().setPreferredColumn(pref);
        moveCursorTo(pos, select);
        return;
    }
    pos.column = doc()->lineText(pos.line).length();
    moveCursorTo(pos, select);
}

void EditorInput::moveToLineStart(bool select)
{
    TextPosition pos = sel().cursorPosition();
    pos.column = 0;
    moveCursorTo(pos, select);
}

void EditorInput::moveToLineEnd(bool select)
{
    TextPosition pos = sel().cursorPosition();
    pos.column = doc()->lineText(pos.line).length();
    moveCursorTo(pos, select);
}

void EditorInput::moveToDocStart(bool select)
{
    moveCursorTo({0, 0}, select);
}

void EditorInput::moveToDocEnd(bool select)
{
    int lastLine = doc()->lineCount() - 1;
    int lastCol = doc()->lineText(lastLine).length();
    moveCursorTo({lastLine, lastCol}, select);
}

void EditorInput::pageUp(bool select)
{
    int linesPerPage = qMax(1, (int)(m_editor->viewport()->height() / layout()->defaultLineHeight()));
    TextPosition pos = sel().cursorPosition();
    pos.line = qMax(0, pos.line - linesPerPage);
    pos.column = qMin(pos.column, doc()->lineText(pos.line).length());
    moveCursorTo(pos, select);
}

void EditorInput::pageDown(bool select)
{
    int linesPerPage = qMax(1, (int)(m_editor->viewport()->height() / layout()->defaultLineHeight()));
    TextPosition pos = sel().cursorPosition();
    pos.line = qMin(doc()->lineCount() - 1, pos.line + linesPerPage);
    pos.column = qMin(pos.column, doc()->lineText(pos.line).length());
    moveCursorTo(pos, select);
}

void EditorInput::undo()
{
    if (doc()->canUndo()) {
        doc()->undo();
    }
}

void EditorInput::redo()
{
    if (doc()->canRedo()) {
        doc()->redo();
    }
}

void EditorInput::selectAll()
{
    TextPosition start = {0, 0};
    int lastLine = doc()->lineCount() - 1;
    int lastCol = doc()->lineText(lastLine).length();
    TextPosition end = {lastLine, lastCol};
    sel().setSelection(start, end);
}

void EditorInput::indent()
{
    insertText(QStringLiteral("    "));
}

void EditorInput::unindent()
{
    TextPosition pos = sel().cursorPosition();
    QString line = doc()->lineText(pos.line);
    int lineStart = doc()->lineToOffset(pos.line);

    // 移除行首最多4个空格
    int removeCount = 0;
    for (int i = 0; i < 4 && i < line.length(); ++i) {
        if (line[i] == ' ') removeCount++;
        else if (line[i] == '\t') { removeCount++; break; }
        else break;
    }

    if (removeCount > 0) {
        doc()->remove(lineStart, removeCount);
        pos.column = qMax(0, pos.column - removeCount);
        sel().setCursorPosition(pos);
        sel().resetPreferredColumn();
    }
}

void EditorInput::cut() {
    copy();
    if (sel().hasSelection()) {
        TextPosition start = sel().range().start();
        TextPosition end = sel().range().end();
        int startOff = posToOffset(start);
        int endOff = posToOffset(end);
        doc()->remove(startOff, endOff - startOff);
        sel().setCursorPosition(start);
        sel().resetPreferredColumn();
    }
}

void EditorInput::copy() {
    if (sel().hasSelection()) {
        TextPosition start = sel().range().start();
        TextPosition end = sel().range().end();
        int startOff = posToOffset(start);
        int endOff = posToOffset(end);
        QString text = doc()->textAt(startOff, endOff - startOff);
        QApplication::clipboard()->setText(text);
    }
}

void EditorInput::paste() {
    auto* cb = QApplication::clipboard();
    const QMimeData* mime = cb->mimeData();

    // [plan B7 2026-05-12] 剪贴板含图片 → 保存到 ./images/ 并插入 ![](images/...)
    if (mime && mime->hasImage()) {
        QString relPath = saveClipboardImageToImagesDir();
        if (!relPath.isEmpty()) {
            insertText(QStringLiteral("![](%1)").arg(relPath));
            return;
        }
        // 保存失败（未保存文档 / 写入失败）→ fallback 到下面的文本路径
    }

    QString text = cb->text();
    if (text.isEmpty()) return;

    // [plan B6 2026-05-12] 粘贴 URL 智能转 Markdown 链接：剪贴板 trim 后是单一 URL
    // 且当前有选区 → 替换选区为 [选中文字](url)。否则走默认 paste 路径。
    const QString trimmed = text.trimmed();
    static const QRegularExpression urlRe(
        QStringLiteral("^https?://\\S+$"),
        QRegularExpression::CaseInsensitiveOption);
    const bool isSingleUrl = !trimmed.contains('\n')
                          && urlRe.match(trimmed).hasMatch();
    if (isSingleUrl && sel().hasSelection()) {
        TextPosition start = sel().range().start();
        TextPosition end = sel().range().end();
        int startOff = posToOffset(start);
        int endOff = posToOffset(end);
        QString selected = doc()->textAt(startOff, endOff - startOff);

        doc()->remove(startOff, endOff - startOff);
        const QString wrapped = QStringLiteral("[%1](%2)").arg(selected, trimmed);
        doc()->insert(startOff, wrapped);
        sel().setCursorPosition(offsetToPos(startOff + wrapped.length()));
        return;
    }

    insertText(text);
}

// [plan B7] 把剪贴板图片保存到 ./images/，返回相对路径（"images/<filename>.png"）。
// 未保存文档 / 文件写入失败 / 剪贴板无图片 → 返回空。
QString EditorInput::saveClipboardImageToImagesDir()
{
    auto* cb = QApplication::clipboard();
    const QMimeData* mime = cb->mimeData();
    if (!mime || !mime->hasImage()) return QString();

    QImage img = qvariant_cast<QImage>(mime->imageData());
    if (img.isNull()) return QString();

    if (!doc() || doc()->filePath().isEmpty()) {
        // 未保存文档无 documentDir 锚点——简化策略：要求先保存。
        // 调用方（paste）回退到默认文本路径。
        return QString();
    }

    QFileInfo docInfo(doc()->filePath());
    QString imagesDir = docInfo.absolutePath() + QStringLiteral("/images");
    if (!QDir().mkpath(imagesDir)) return QString();

    QString baseName = docInfo.completeBaseName();
    if (baseName.isEmpty()) baseName = QStringLiteral("image");
    QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    QString filename = QStringLiteral("%1-%2.png").arg(baseName, timestamp);
    int counter = 1;
    while (QFileInfo::exists(imagesDir + QLatin1Char('/') + filename)) {
        filename = QStringLiteral("%1-%2-%3.png").arg(baseName, timestamp).arg(counter++);
    }

    QString fullPath = imagesDir + QLatin1Char('/') + filename;
    if (!img.save(fullPath, "PNG")) return QString();
    return QStringLiteral("images/%1").arg(filename);
}

// [plan B8] 把已存在的图片文件复制到 ./images/，返回相对路径。
QString EditorInput::copyImageFileToImagesDir(const QString& srcPath)
{
    if (srcPath.isEmpty()) return QString();
    QFileInfo src(srcPath);
    if (!src.exists() || !src.isFile()) return QString();

    if (!doc() || doc()->filePath().isEmpty()) return QString();

    QFileInfo docInfo(doc()->filePath());
    QString imagesDir = docInfo.absolutePath() + QStringLiteral("/images");
    if (!QDir().mkpath(imagesDir)) return QString();

    QString baseName = src.completeBaseName();
    QString suffix = src.suffix().toLower();
    if (suffix.isEmpty()) suffix = QStringLiteral("png");
    QString filename = QStringLiteral("%1.%2").arg(baseName, suffix);
    int counter = 1;
    while (QFileInfo::exists(imagesDir + QLatin1Char('/') + filename)) {
        filename = QStringLiteral("%1-%2.%3").arg(baseName).arg(counter++).arg(suffix);
    }

    QString fullPath = imagesDir + QLatin1Char('/') + filename;
    if (!QFile::copy(srcPath, fullPath)) return QString();
    return QStringLiteral("images/%1").arg(filename);
}

void EditorInput::wrapSelection(const QString& before, const QString& after)
{
    if (sel().hasSelection()) {
        // 有选区：在选区两端包裹标记
        TextPosition start = sel().range().start();
        TextPosition end = sel().range().end();
        int startOff = posToOffset(start);
        int endOff = posToOffset(end);
        QString selected = doc()->textAt(startOff, endOff - startOff);

        doc()->remove(startOff, endOff - startOff);
        sel().setCursorPosition(start);

        QString wrapped = before + selected + after;
        doc()->insert(startOff, wrapped);

        // 选中包裹后的内容（不含标记）
        TextPosition newStart = offsetToPos(startOff + before.length());
        TextPosition newEnd = offsetToPos(startOff + before.length() + selected.length());
        sel().setCursorPosition(newStart);
        sel().extendSelection(newEnd);
    } else {
        // 无选区：插入标记对并把光标放中间
        int offset = posToOffset(sel().cursorPosition());
        QString text = before + after;
        doc()->insert(offset, text);
        TextPosition mid = offsetToPos(offset + before.length());
        sel().setCursorPosition(mid);
    }
    sel().resetPreferredColumn();
}

void EditorInput::moveCursorTo(TextPosition pos, bool select)
{
    if (select) {
        sel().extendSelection(pos);
    } else {
        sel().setCursorPosition(pos);
    }
}
