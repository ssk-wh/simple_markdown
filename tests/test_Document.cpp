#include <gtest/gtest.h>
#include "Document.h"
#include <QTemporaryFile>
#include <QFile>
#include <QSignalSpy>

TEST(Document, InitialState) {
    Document doc;
    EXPECT_TRUE(doc.isEmpty());
    EXPECT_EQ(doc.length(), 0);
    EXPECT_FALSE(doc.isModified());
    EXPECT_EQ(doc.lineCount(), 1);
}

TEST(Document, InsertEmitsSignal) {
    Document doc;
    QSignalSpy spy(&doc, &Document::textChanged);
    doc.insert(0, "Hello");
    ASSERT_EQ(spy.count(), 1);
    auto args = spy.takeFirst();
    EXPECT_EQ(args.at(0).toInt(), 0);
    EXPECT_EQ(args.at(1).toInt(), 0);
    EXPECT_EQ(args.at(2).toInt(), 5);
    EXPECT_EQ(doc.text(), "Hello");
}

TEST(Document, InsertMarksModified) {
    Document doc;
    doc.insert(0, "A");
    EXPECT_TRUE(doc.isModified());
}

TEST(Document, UndoRedo) {
    Document doc;
    doc.insert(0, "Hello");
    doc.insert(5, " World");
    EXPECT_EQ(doc.text(), "Hello World");
    doc.undo();
    EXPECT_EQ(doc.text(), "Hello");
    doc.undo();
    EXPECT_EQ(doc.text(), "");
    doc.redo();
    EXPECT_EQ(doc.text(), "Hello");
    doc.redo();
    EXPECT_EQ(doc.text(), "Hello World");
}

TEST(Document, UndoRestoresModifiedState) {
    Document doc;
    doc.insert(0, "A");
    EXPECT_TRUE(doc.isModified());
    doc.undo();
    EXPECT_FALSE(doc.isModified());
}

TEST(Document, RemoveAndUndo) {
    Document doc;
    doc.insert(0, "ABCDE");
    doc.remove(1, 3);
    EXPECT_EQ(doc.text(), "AE");
    doc.undo();
    EXPECT_EQ(doc.text(), "ABCDE");
}

TEST(Document, Replace) {
    Document doc;
    doc.insert(0, "Hello World");
    doc.replace(6, 5, "Earth");
    EXPECT_EQ(doc.text(), "Hello Earth");
}

TEST(Document, LoadAndSaveFile) {
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    ASSERT_TRUE(tmp.open());
    tmp.write("Line1\nLine2\nLine3");
    tmp.flush();
    QString path = tmp.fileName();
    tmp.close();

    Document doc;
    ASSERT_TRUE(doc.loadFromFile(path));
    EXPECT_EQ(doc.text(), "Line1\nLine2\nLine3");
    EXPECT_EQ(doc.lineCount(), 3);
    EXPECT_FALSE(doc.isModified());

    doc.insert(5, "A");
    EXPECT_TRUE(doc.isModified());

    QTemporaryFile out;
    out.setAutoRemove(true);
    ASSERT_TRUE(out.open());
    QString outPath = out.fileName();
    out.close();

    ASSERT_TRUE(doc.saveToFile(outPath));
    EXPECT_FALSE(doc.isModified());

    QFile f(outPath);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_EQ(f.readAll(), QByteArray("Line1A\nLine2\nLine3"));
}

TEST(Document, CRLFHandling) {
    QTemporaryFile tmp;
    tmp.setAutoRemove(true);
    ASSERT_TRUE(tmp.open());
    tmp.write("Line1\r\nLine2\r\nLine3");
    tmp.flush();
    QString path = tmp.fileName();
    tmp.close();

    Document doc;
    ASSERT_TRUE(doc.loadFromFile(path));
    EXPECT_EQ(doc.text(), "Line1\nLine2\nLine3");
    EXPECT_EQ(doc.detectedLineEnding(), Document::CRLF);

    QTemporaryFile out;
    out.setAutoRemove(true);
    ASSERT_TRUE(out.open());
    QString outPath = out.fileName();
    out.close();

    ASSERT_TRUE(doc.saveToFile(outPath));
    QFile f(outPath);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    EXPECT_EQ(f.readAll(), QByteArray("Line1\r\nLine2\r\nLine3"));
}

TEST(Document, LineQueries) {
    Document doc;
    doc.insert(0, "First\nSecond\nThird");
    EXPECT_EQ(doc.lineCount(), 3);
    EXPECT_EQ(doc.lineText(0), "First");
    EXPECT_EQ(doc.lineText(1), "Second");
    EXPECT_EQ(doc.lineText(2), "Third");
}

TEST(Document, SelectionIntegration) {
    Document doc;
    doc.insert(0, "Hello World");
    doc.selection().setSelection({0, 0}, {0, 5});
    EXPECT_TRUE(doc.selection().hasSelection());
}

TEST(Document, LoadNonExistentFile) {
    Document doc;
    EXPECT_FALSE(doc.loadFromFile("__nonexistent__12345.txt"));
}
