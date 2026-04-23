#include "Selection.h"

// TextPosition 运算符

bool TextPosition::operator==(const TextPosition& other) const {
    return line == other.line && column == other.column;
}

bool TextPosition::operator!=(const TextPosition& other) const {
    return !(*this == other);
}

bool TextPosition::operator<(const TextPosition& other) const {
    if (line != other.line) return line < other.line;
    return column < other.column;
}

bool TextPosition::operator<=(const TextPosition& other) const {
    return !(other < *this);
}

bool TextPosition::operator>(const TextPosition& other) const {
    return other < *this;
}

bool TextPosition::operator>=(const TextPosition& other) const {
    return !(*this < other);
}

// 选区范围

bool SelectionRange::isEmpty() const {
    return anchor == cursor;
}

bool SelectionRange::isForward() const {
    return anchor <= cursor;
}

TextPosition SelectionRange::start() const {
    return anchor < cursor ? anchor : cursor;
}

TextPosition SelectionRange::end() const {
    return anchor < cursor ? cursor : anchor;
}

// 选区管理

Selection::Selection() = default;

TextPosition Selection::cursorPosition() const {
    return m_range.cursor;
}

void Selection::setCursorPosition(TextPosition pos) {
    m_range.anchor = pos;
    m_range.cursor = pos;
}

SelectionRange Selection::range() const {
    return m_range;
}

bool Selection::hasSelection() const {
    return !m_range.isEmpty();
}

void Selection::setSelection(TextPosition anchor, TextPosition cursor) {
    m_range.anchor = anchor;
    m_range.cursor = cursor;
}

void Selection::clearSelection() {
    m_range.anchor = m_range.cursor;
}

void Selection::extendSelection(TextPosition newCursor) {
    m_range.cursor = newCursor;
}

int Selection::preferredColumn() const {
    return m_preferredColumn;
}

void Selection::setPreferredColumn(int col) {
    m_preferredColumn = col;
}

void Selection::resetPreferredColumn() {
    m_preferredColumn = -1;
}
