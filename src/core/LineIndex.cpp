#include "LineIndex.h"

LineIndex::LineIndex() = default;

void LineIndex::build(const QChar* /*data*/, int /*length*/) {
    // Phase 1: 骨架实现，PieceTable 内部使用 lineFeedPrefix 方案
}

int LineIndex::lineCount() const { return 1; }
int LineIndex::offsetToLine(int /*offset*/) const { return 0; }
int LineIndex::lineToOffset(int /*line*/) const { return 0; }
int LineIndex::lineLength(int /*line*/) const { return 0; }
