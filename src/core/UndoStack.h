#pragma once
#include <QString>
#include <vector>
#include <cstdint>

struct EditOperation {
    int offset;
    QString removedText;
    QString addedText;
    int64_t timestamp;
};

class UndoStack {
public:
    UndoStack();
    void push(int offset, const QString& removedText, const QString& addedText);
    bool canUndo() const;
    EditOperation undo();
    bool canRedo() const;
    EditOperation redo();
    void setSavePoint();
    bool isAtSavePoint() const;
    void clear();
    void setMergeInterval(int64_t ms);

private:
    std::vector<EditOperation> m_undoStack;
    std::vector<EditOperation> m_redoStack;
    int m_savePoint = 0;
    int64_t m_mergeInterval = 300;
    bool shouldMerge(const EditOperation& existing, int offset,
                     const QString& removedText, const QString& addedText) const;
    int64_t currentTimestamp() const;
};
