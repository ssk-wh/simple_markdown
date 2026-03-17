#pragma once
#include <QString>
#include <vector>
#include <cstdint>

struct Piece {
    enum Source : uint8_t { Original, Add };
    Source source;
    uint32_t start;
    uint32_t length;
    uint32_t lineFeeds;
};

class PieceTable {
public:
    PieceTable();
    explicit PieceTable(const QString& text);

    void insert(int offset, const QString& text);
    void remove(int offset, int length);
    void replace(int offset, int length, const QString& text);

    QString text() const;
    QString textAt(int offset, int length) const;
    int length() const;
    bool isEmpty() const;

    int lineCount() const;
    QString lineText(int line) const;
    int offsetToLine(int offset) const;
    int lineToOffset(int line) const;

private:
    QString m_original;
    QString m_add;
    std::vector<Piece> m_pieces;
    std::vector<uint32_t> m_lineFeedPrefix;

    const QString& bufferFor(Piece::Source source) const;
    int findPieceAtOffset(int offset, int& offsetInPiece) const;
    static uint32_t countLineFeeds(const QString& buf, int start, int length);
    void updateLineFeedPrefix();
};
