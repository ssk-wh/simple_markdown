#include "UndoStack.h"
#include <QDateTime>

UndoStack::UndoStack() = default;

int64_t UndoStack::currentTimestamp() const
{
    return QDateTime::currentMSecsSinceEpoch();
}

bool UndoStack::shouldMerge(const EditOperation& existing, int offset,
                            const QString& removedText, const QString& addedText) const
{
    if (m_undoStack.empty())
        return false;

    int64_t now = currentTimestamp();
    if (now - existing.timestamp >= m_mergeInterval)
        return false;

    // Insert merge: both are pure inserts, consecutive offset, single non-space non-newline char
    if (existing.removedText.isEmpty() && removedText.isEmpty()) {
        if (addedText.length() == 1
            && addedText != " " && addedText != "\n"
            && offset == existing.offset + existing.addedText.length()) {
            return true;
        }
    }

    // Backspace merge: both are pure deletes, consecutive offset (going backwards), single char
    if (existing.addedText.isEmpty() && addedText.isEmpty()) {
        if (removedText.length() == 1
            && offset == existing.offset - 1) {
            return true;
        }
    }

    return false;
}

void UndoStack::push(int offset, const QString& removedText, const QString& addedText)
{
    // Clear redo stack - new edit branch
    if (!m_redoStack.empty()) {
        // If savePoint was in the redo future, invalidate it
        if (m_savePoint > static_cast<int>(m_undoStack.size())) {
            m_savePoint = -1;
        }
        m_redoStack.clear();
    }

    int64_t ts = currentTimestamp();

    bool mergeAllowed = !m_undoStack.empty()
        && shouldMerge(m_undoStack.back(), offset, removedText, addedText)
        && m_savePoint != static_cast<int>(m_undoStack.size());

    if (mergeAllowed) {
        auto& top = m_undoStack.back();
        if (removedText.isEmpty()) {
            // Insert merge
            top.addedText += addedText;
        } else {
            // Backspace merge
            top.removedText = removedText + top.removedText;
            top.offset = offset;
        }
        top.timestamp = ts;
    } else {
        m_undoStack.push_back({offset, removedText, addedText, ts});
    }
}

bool UndoStack::canUndo() const
{
    return !m_undoStack.empty();
}

EditOperation UndoStack::undo()
{
    EditOperation op = m_undoStack.back();
    m_undoStack.pop_back();
    m_redoStack.push_back(op);
    return op;
}

bool UndoStack::canRedo() const
{
    return !m_redoStack.empty();
}

EditOperation UndoStack::redo()
{
    EditOperation op = m_redoStack.back();
    m_redoStack.pop_back();
    m_undoStack.push_back(op);
    return op;
}

void UndoStack::setSavePoint()
{
    m_savePoint = static_cast<int>(m_undoStack.size());
}

bool UndoStack::isAtSavePoint() const
{
    return static_cast<int>(m_undoStack.size()) == m_savePoint;
}

void UndoStack::clear()
{
    m_undoStack.clear();
    m_redoStack.clear();
    m_savePoint = 0;
}

void UndoStack::setMergeInterval(int64_t ms)
{
    m_mergeInterval = ms;
}
