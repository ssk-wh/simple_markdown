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

    // 插入合并：两次都是纯插入，偏移量连续，单个非空格非换行字符
    if (existing.removedText.isEmpty() && removedText.isEmpty()) {
        if (addedText.length() == 1
            && addedText != " " && addedText != "\n"
            && offset == existing.offset + existing.addedText.length()) {
            return true;
        }
    }

    // 退格合并：两次都是纯删除，偏移量连续（向前递减），单个字符
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
    // 清空重做栈 - 新的编辑分支
    if (!m_redoStack.empty()) {
        // 如果保存点位于重做的未来位置，则使其失效
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
            // 插入合并
            top.addedText += addedText;
        } else {
            // 退格合并
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
