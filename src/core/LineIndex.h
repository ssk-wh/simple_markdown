#pragma once
#include <QString>
#include <vector>

// 行索引：维护每行的起始偏移量数组
// 用于大文件的快速行号查询，Phase 1 中为骨架实现
class LineIndex {
public:
    LineIndex();

    void build(const QChar* data, int length);

    int lineCount() const;
    int offsetToLine(int offset) const;
    int lineToOffset(int line) const;
    int lineLength(int line) const;

private:
    std::vector<int> m_lineStarts;
    int m_totalLength = 0;
};
