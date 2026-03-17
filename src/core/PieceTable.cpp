#include "PieceTable.h"

PieceTable::PieceTable() = default;

PieceTable::PieceTable(const QString& text)
    : m_original(text)
{
    if (!text.isEmpty()) {
        Piece p;
        p.source = Piece::Original;
        p.start = 0;
        p.length = static_cast<uint32_t>(text.length());
        p.lineFeeds = countLineFeeds(m_original, 0, text.length());
        m_pieces.push_back(p);
        updateLineFeedPrefix();
    }
}

void PieceTable::insert(int offset, const QString& text)
{
    if (text.isEmpty())
        return;

    int addStart = m_add.length();
    m_add += text;

    Piece newPiece;
    newPiece.source = Piece::Add;
    newPiece.start = static_cast<uint32_t>(addStart);
    newPiece.length = static_cast<uint32_t>(text.length());
    newPiece.lineFeeds = countLineFeeds(m_add, addStart, text.length());

    if (m_pieces.empty()) {
        m_pieces.push_back(newPiece);
        updateLineFeedPrefix();
        return;
    }

    int offsetInPiece = 0;
    int idx = findPieceAtOffset(offset, offsetInPiece);

    // offset is at the very end (past all pieces)
    if (idx < 0) {
        m_pieces.push_back(newPiece);
        updateLineFeedPrefix();
        return;
    }

    if (offsetInPiece == 0) {
        // Insert before this piece
        m_pieces.insert(m_pieces.begin() + idx, newPiece);
    } else {
        // Split the piece
        const Piece& old = m_pieces[idx];
        Piece left;
        left.source = old.source;
        left.start = old.start;
        left.length = static_cast<uint32_t>(offsetInPiece);
        left.lineFeeds = countLineFeeds(bufferFor(old.source), old.start, offsetInPiece);

        Piece right;
        right.source = old.source;
        right.start = old.start + static_cast<uint32_t>(offsetInPiece);
        right.length = old.length - static_cast<uint32_t>(offsetInPiece);
        right.lineFeeds = countLineFeeds(bufferFor(old.source), right.start, right.length);

        m_pieces[idx] = left;
        m_pieces.insert(m_pieces.begin() + idx + 1, newPiece);
        m_pieces.insert(m_pieces.begin() + idx + 2, right);
    }

    updateLineFeedPrefix();
}

void PieceTable::remove(int offset, int length)
{
    if (length <= 0 || m_pieces.empty())
        return;

    int startOffInPiece = 0;
    int startIdx = findPieceAtOffset(offset, startOffInPiece);
    if (startIdx < 0)
        return;

    int endOffInPiece = 0;
    int endIdx = findPieceAtOffset(offset + length, endOffInPiece);

    // If end is past all pieces, remove to the end
    if (endIdx < 0) {
        // Truncate start piece
        if (startOffInPiece == 0) {
            m_pieces.erase(m_pieces.begin() + startIdx, m_pieces.end());
        } else {
            Piece& p = m_pieces[startIdx];
            p.length = static_cast<uint32_t>(startOffInPiece);
            p.lineFeeds = countLineFeeds(bufferFor(p.source), p.start, p.length);
            m_pieces.erase(m_pieces.begin() + startIdx + 1, m_pieces.end());
        }
        updateLineFeedPrefix();
        return;
    }

    if (startIdx == endIdx) {
        // Remove within the same piece
        const Piece& old = m_pieces[startIdx];

        std::vector<Piece> replacement;

        if (startOffInPiece > 0) {
            Piece left;
            left.source = old.source;
            left.start = old.start;
            left.length = static_cast<uint32_t>(startOffInPiece);
            left.lineFeeds = countLineFeeds(bufferFor(old.source), left.start, left.length);
            replacement.push_back(left);
        }

        if (endOffInPiece < static_cast<int>(old.length)) {
            Piece right;
            right.source = old.source;
            right.start = old.start + static_cast<uint32_t>(endOffInPiece);
            right.length = old.length - static_cast<uint32_t>(endOffInPiece);
            right.lineFeeds = countLineFeeds(bufferFor(old.source), right.start, right.length);
            replacement.push_back(right);
        }

        m_pieces.erase(m_pieces.begin() + startIdx);
        m_pieces.insert(m_pieces.begin() + startIdx, replacement.begin(), replacement.end());
    } else {
        // Remove across multiple pieces
        std::vector<Piece> replacement;

        // Left remainder of start piece
        if (startOffInPiece > 0) {
            const Piece& sp = m_pieces[startIdx];
            Piece left;
            left.source = sp.source;
            left.start = sp.start;
            left.length = static_cast<uint32_t>(startOffInPiece);
            left.lineFeeds = countLineFeeds(bufferFor(sp.source), left.start, left.length);
            replacement.push_back(left);
        }

        // Right remainder of end piece
        if (endOffInPiece < static_cast<int>(m_pieces[endIdx].length)) {
            const Piece& ep = m_pieces[endIdx];
            Piece right;
            right.source = ep.source;
            right.start = ep.start + static_cast<uint32_t>(endOffInPiece);
            right.length = ep.length - static_cast<uint32_t>(endOffInPiece);
            right.lineFeeds = countLineFeeds(bufferFor(ep.source), right.start, right.length);
            replacement.push_back(right);
        }

        m_pieces.erase(m_pieces.begin() + startIdx, m_pieces.begin() + endIdx + 1);
        m_pieces.insert(m_pieces.begin() + startIdx, replacement.begin(), replacement.end());
    }

    // Remove zero-length pieces
    auto it = m_pieces.begin();
    while (it != m_pieces.end()) {
        if (it->length == 0)
            it = m_pieces.erase(it);
        else
            ++it;
    }

    updateLineFeedPrefix();
}

void PieceTable::replace(int offset, int length, const QString& text)
{
    remove(offset, length);
    insert(offset, text);
}

QString PieceTable::text() const
{
    QString result;
    for (const auto& p : m_pieces) {
        result += bufferFor(p.source).mid(p.start, p.length);
    }
    return result;
}

QString PieceTable::textAt(int offset, int length) const
{
    if (length <= 0)
        return QString();

    QString result;
    int remaining = length;
    int currentOffset = 0;

    for (const auto& p : m_pieces) {
        int pieceEnd = currentOffset + static_cast<int>(p.length);
        if (pieceEnd <= offset) {
            currentOffset = pieceEnd;
            continue;
        }

        int startInPiece = 0;
        if (offset > currentOffset)
            startInPiece = offset - currentOffset;

        int charsFromPiece = static_cast<int>(p.length) - startInPiece;
        if (charsFromPiece > remaining)
            charsFromPiece = remaining;

        result += bufferFor(p.source).mid(p.start + startInPiece, charsFromPiece);
        remaining -= charsFromPiece;

        if (remaining <= 0)
            break;

        currentOffset = pieceEnd;
    }

    return result;
}

int PieceTable::length() const
{
    int total = 0;
    for (const auto& p : m_pieces)
        total += static_cast<int>(p.length);
    return total;
}

bool PieceTable::isEmpty() const
{
    return length() == 0;
}

int PieceTable::lineCount() const
{
    if (m_pieces.empty())
        return 1;

    uint32_t totalLF = 0;
    for (const auto& p : m_pieces)
        totalLF += p.lineFeeds;
    return static_cast<int>(1 + totalLF);
}

QString PieceTable::lineText(int line) const
{
    int start = lineToOffset(line);
    int totalLen = length();

    if (line + 1 < lineCount()) {
        int end = lineToOffset(line + 1);
        // end points to the first char of next line, so end-1 is the '\n'
        return textAt(start, end - 1 - start);
    } else {
        // Last line: from start to end of text
        return textAt(start, totalLen - start);
    }
}

int PieceTable::offsetToLine(int offset) const
{
    int currentOffset = 0;
    int lineNum = 0;

    for (const auto& p : m_pieces) {
        int pieceEnd = currentOffset + static_cast<int>(p.length);

        if (offset < pieceEnd) {
            // offset is within this piece
            const QString& buf = bufferFor(p.source);
            int posInPiece = offset - currentOffset;
            for (int i = 0; i < posInPiece; ++i) {
                if (buf.at(p.start + i) == QChar('\n'))
                    ++lineNum;
            }
            return lineNum;
        }

        lineNum += static_cast<int>(p.lineFeeds);
        currentOffset = pieceEnd;
    }

    return lineNum;
}

int PieceTable::lineToOffset(int line) const
{
    if (line <= 0)
        return 0;

    int currentOffset = 0;
    int currentLine = 0;

    for (const auto& p : m_pieces) {
        if (currentLine + static_cast<int>(p.lineFeeds) >= line) {
            // Target line starts within this piece
            const QString& buf = bufferFor(p.source);
            for (uint32_t i = 0; i < p.length; ++i) {
                if (buf.at(p.start + i) == QChar('\n')) {
                    ++currentLine;
                    if (currentLine == line)
                        return currentOffset + static_cast<int>(i) + 1;
                }
            }
        }
        currentLine += static_cast<int>(p.lineFeeds);
        currentOffset += static_cast<int>(p.length);
    }

    return currentOffset;
}

const QString& PieceTable::bufferFor(Piece::Source source) const
{
    return source == Piece::Original ? m_original : m_add;
}

int PieceTable::findPieceAtOffset(int offset, int& offsetInPiece) const
{
    int cumulative = 0;
    for (int i = 0; i < static_cast<int>(m_pieces.size()); ++i) {
        int pieceLen = static_cast<int>(m_pieces[i].length);
        if (offset < cumulative + pieceLen) {
            offsetInPiece = offset - cumulative;
            return i;
        }
        cumulative += pieceLen;
    }

    // offset == total length (past the end)
    if (offset == cumulative) {
        offsetInPiece = 0;
        return -1; // signals "past the end"
    }

    offsetInPiece = 0;
    return -1;
}

uint32_t PieceTable::countLineFeeds(const QString& buf, int start, int length)
{
    uint32_t count = 0;
    for (int i = start; i < start + length; ++i) {
        if (buf.at(i) == QChar('\n'))
            ++count;
    }
    return count;
}

void PieceTable::updateLineFeedPrefix()
{
    m_lineFeedPrefix.resize(m_pieces.size());
    uint32_t sum = 0;
    for (size_t i = 0; i < m_pieces.size(); ++i) {
        sum += m_pieces[i].lineFeeds;
        m_lineFeedPrefix[i] = sum;
    }
}
