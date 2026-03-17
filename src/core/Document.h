#pragma once
#include <QObject>
#include <QString>
#include "PieceTable.h"
#include "UndoStack.h"
#include "Selection.h"

class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject* parent = nullptr);

    // 文件 I/O
    bool loadFromFile(const QString& filePath);
    bool saveToFile(const QString& filePath = QString());
    QString filePath() const;

    // 文本操作
    void insert(int offset, const QString& text);
    void remove(int offset, int length);
    void replace(int offset, int length, const QString& text);

    // 文本查询（代理到 PieceTable）
    QString text() const;
    QString textAt(int offset, int length) const;
    int length() const;
    bool isEmpty() const;
    int lineCount() const;
    QString lineText(int line) const;
    int offsetToLine(int offset) const;
    int lineToOffset(int line) const;

    // 撤销/重做
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();

    // 选区
    Selection& selection();
    const Selection& selection() const;

    // 修改状态
    bool isModified() const;
    void setModified(bool modified);

    // 换行风格
    enum LineEnding { LF, CRLF };
    LineEnding detectedLineEnding() const;

signals:
    void textChanged(int offset, int removedLength, int addedLength);
    void modifiedChanged(bool modified);

private:
    PieceTable m_pieceTable;
    UndoStack m_undoStack;
    Selection m_selection;
    QString m_filePath;
    LineEnding m_lineEnding = LF;

    static QString normalizeLineEndings(const QString& text, LineEnding* detected = nullptr);
    static QString denormalizeLineEndings(const QString& text, LineEnding lineEnding);
};
