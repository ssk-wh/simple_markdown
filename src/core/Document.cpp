#include "Document.h"
#include <QFile>
#include <QTextStream>

Document::Document(QObject* parent)
    : QObject(parent)
{
    m_undoStack.setSavePoint();
}

// --- 文件 I/O ---

bool Document::loadFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QByteArray raw = file.readAll();
    file.close();

    QString content = QString::fromUtf8(raw);
    LineEnding detected = LF;
    content = normalizeLineEndings(content, &detected);

    m_pieceTable = PieceTable(content);
    m_undoStack.clear();
    m_undoStack.setSavePoint();
    m_filePath = filePath;
    m_lineEnding = detected;
    m_selection.clearSelection();

    emit textChanged(0, 0, m_pieceTable.length());
    emit modifiedChanged(false);

    return true;
}

bool Document::saveToFile(const QString& filePath)
{
    QString path = filePath.isEmpty() ? m_filePath : filePath;
    if (path.isEmpty())
        return false;

    QString content = text();
    content = denormalizeLineEndings(content, m_lineEnding);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    file.write(content.toUtf8());
    file.close();

    m_filePath = path;
    bool wasModified = isModified();
    m_forceModified = false;
    m_undoStack.setSavePoint();
    if (wasModified)
        emit modifiedChanged(false);

    return true;
}

QString Document::filePath() const
{
    return m_filePath;
}

// --- 文本操作 ---

void Document::insert(int offset, const QString& text)
{
    bool wasModified = isModified();
    m_pieceTable.insert(offset, text);
    m_undoStack.push(offset, QString(), text);
    emit textChanged(offset, 0, text.length());
    if (!wasModified && isModified())
        emit modifiedChanged(true);
}

void Document::remove(int offset, int length)
{
    bool wasModified = isModified();
    QString removed = m_pieceTable.textAt(offset, length);
    m_pieceTable.remove(offset, length);
    m_undoStack.push(offset, removed, QString());
    emit textChanged(offset, length, 0);
    if (!wasModified && isModified())
        emit modifiedChanged(true);
}

void Document::replace(int offset, int length, const QString& text)
{
    bool wasModified = isModified();
    QString removed = m_pieceTable.textAt(offset, length);
    m_pieceTable.remove(offset, length);
    m_pieceTable.insert(offset, text);
    m_undoStack.push(offset, removed, text);
    emit textChanged(offset, length, text.length());
    if (!wasModified && isModified())
        emit modifiedChanged(true);
}

// --- 文本查询 ---

QString Document::text() const { return m_pieceTable.text(); }
QString Document::textAt(int offset, int length) const { return m_pieceTable.textAt(offset, length); }
int Document::length() const { return m_pieceTable.length(); }
bool Document::isEmpty() const { return m_pieceTable.isEmpty(); }
int Document::lineCount() const { return m_pieceTable.lineCount(); }
QString Document::lineText(int line) const { return m_pieceTable.lineText(line); }
int Document::offsetToLine(int offset) const { return m_pieceTable.offsetToLine(offset); }
int Document::lineToOffset(int line) const { return m_pieceTable.lineToOffset(line); }

// --- 撤销/重做 ---

bool Document::canUndo() const { return m_undoStack.canUndo(); }
bool Document::canRedo() const { return m_undoStack.canRedo(); }

void Document::undo()
{
    if (!m_undoStack.canUndo())
        return;

    bool wasModified = isModified();
    EditOperation op = m_undoStack.undo();

    // 反向执行：先移除 addedText，再插入 removedText
    if (!op.addedText.isEmpty())
        m_pieceTable.remove(op.offset, op.addedText.length());
    if (!op.removedText.isEmpty())
        m_pieceTable.insert(op.offset, op.removedText);

    int removedLen = op.addedText.length();
    int addedLen = op.removedText.length();
    emit textChanged(op.offset, removedLen, addedLen);

    if (wasModified != isModified())
        emit modifiedChanged(isModified());
}

void Document::redo()
{
    if (!m_undoStack.canRedo())
        return;

    bool wasModified = isModified();
    EditOperation op = m_undoStack.redo();

    // 正向执行：先移除 removedText，再插入 addedText
    if (!op.removedText.isEmpty())
        m_pieceTable.remove(op.offset, op.removedText.length());
    if (!op.addedText.isEmpty())
        m_pieceTable.insert(op.offset, op.addedText);

    int removedLen = op.removedText.length();
    int addedLen = op.addedText.length();
    emit textChanged(op.offset, removedLen, addedLen);

    if (wasModified != isModified())
        emit modifiedChanged(isModified());
}

// --- 选区 ---

Selection& Document::selection() { return m_selection; }
const Selection& Document::selection() const { return m_selection; }

// --- 修改状态 ---

bool Document::isModified() const { return m_forceModified || !m_undoStack.isAtSavePoint(); }

void Document::setModified(bool modified)
{
    if (modified) {
        m_forceModified = true;
        emit modifiedChanged(true);
    } else {
        m_forceModified = false;
        m_undoStack.setSavePoint();
        emit modifiedChanged(false);
    }
}

// --- 换行风格 ---

Document::LineEnding Document::detectedLineEnding() const { return m_lineEnding; }

QString Document::normalizeLineEndings(const QString& text, LineEnding* detected)
{
    if (text.contains(QLatin1String("\r\n"))) {
        if (detected)
            *detected = CRLF;
        QString result = text;
        result.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        return result;
    }
    if (detected)
        *detected = LF;
    return text;
}

QString Document::denormalizeLineEndings(const QString& text, LineEnding lineEnding)
{
    if (lineEnding == CRLF) {
        QString result = text;
        result.replace(QLatin1String("\n"), QLatin1String("\r\n"));
        return result;
    }
    return text;
}
