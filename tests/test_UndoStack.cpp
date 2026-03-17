#include <gtest/gtest.h>
#include "UndoStack.h"

TEST(UndoStack, InitialState) {
    UndoStack us;
    EXPECT_FALSE(us.canUndo());
    EXPECT_FALSE(us.canRedo());
    EXPECT_TRUE(us.isAtSavePoint());
}

TEST(UndoStack, PushAndUndo) {
    UndoStack us;
    us.push(0, "", "A");
    EXPECT_TRUE(us.canUndo());
    auto op = us.undo();
    EXPECT_EQ(op.offset, 0);
    EXPECT_EQ(op.addedText, "A");
    EXPECT_EQ(op.removedText, "");
    EXPECT_FALSE(us.canUndo());
    EXPECT_TRUE(us.canRedo());
}

TEST(UndoStack, UndoRedo) {
    UndoStack us;
    us.push(0, "", "Hello");
    us.undo();
    auto op = us.redo();
    EXPECT_EQ(op.addedText, "Hello");
    EXPECT_TRUE(us.canUndo());
    EXPECT_FALSE(us.canRedo());
}

TEST(UndoStack, NewPushClearsRedoStack) {
    UndoStack us;
    us.push(0, "", "A");
    us.undo();
    us.push(0, "", "B");
    EXPECT_FALSE(us.canRedo());
}

TEST(UndoStack, MergeContinuousTyping) {
    UndoStack us;
    us.setMergeInterval(10000);
    us.push(0, "", "A");
    us.push(1, "", "B");
    us.push(2, "", "C");
    auto op = us.undo();
    EXPECT_EQ(op.addedText, "ABC");
    EXPECT_FALSE(us.canUndo());
}

TEST(UndoStack, NoMergeOnSpace) {
    UndoStack us;
    us.setMergeInterval(10000);
    us.push(0, "", "A");
    us.push(1, "", " ");
    auto op = us.undo();
    EXPECT_EQ(op.addedText, " ");
    EXPECT_TRUE(us.canUndo());
}

TEST(UndoStack, NoMergeOnNewline) {
    UndoStack us;
    us.setMergeInterval(10000);
    us.push(0, "", "A");
    us.push(1, "", "\n");
    auto op = us.undo();
    EXPECT_EQ(op.addedText, "\n");
    EXPECT_TRUE(us.canUndo());
}

TEST(UndoStack, MergeContinuousBackspace) {
    UndoStack us;
    us.setMergeInterval(10000);
    us.push(3, "D", "");
    us.push(2, "C", "");
    us.push(1, "B", "");
    auto op = us.undo();
    EXPECT_EQ(op.removedText, "BCD");
    EXPECT_EQ(op.offset, 1);
    EXPECT_FALSE(us.canUndo());
}

TEST(UndoStack, NoMergeNonConsecutiveOffset) {
    UndoStack us;
    us.setMergeInterval(10000);
    us.push(0, "", "A");
    us.push(5, "", "B");
    auto op = us.undo();
    EXPECT_EQ(op.addedText, "B");
    EXPECT_TRUE(us.canUndo());
}

TEST(UndoStack, SavePoint) {
    UndoStack us;
    us.push(0, "", "A");
    EXPECT_FALSE(us.isAtSavePoint());
    us.setSavePoint();
    EXPECT_TRUE(us.isAtSavePoint());
    us.push(1, "", "B");
    EXPECT_FALSE(us.isAtSavePoint());
    us.undo();
    EXPECT_TRUE(us.isAtSavePoint());
}

TEST(UndoStack, SavePointAfterUndoPastSave) {
    UndoStack us;
    us.push(0, "", "A");
    us.setSavePoint();
    us.undo();
    EXPECT_FALSE(us.isAtSavePoint());
    us.redo();
    EXPECT_TRUE(us.isAtSavePoint());
}

TEST(UndoStack, Clear) {
    UndoStack us;
    us.push(0, "", "A");
    us.clear();
    EXPECT_FALSE(us.canUndo());
    EXPECT_FALSE(us.canRedo());
    EXPECT_TRUE(us.isAtSavePoint());
}

TEST(UndoStack, MultipleUndoRedo) {
    UndoStack us;
    us.setMergeInterval(0);
    us.push(0, "", "A");
    us.push(1, "", "B");
    us.push(2, "", "C");
    auto op1 = us.undo(); EXPECT_EQ(op1.addedText, "C");
    auto op2 = us.undo(); EXPECT_EQ(op2.addedText, "B");
    auto op3 = us.redo(); EXPECT_EQ(op3.addedText, "B");
    auto op4 = us.redo(); EXPECT_EQ(op4.addedText, "C");
}

TEST(UndoStack, DeleteOperation) {
    UndoStack us;
    us.push(5, "World", "");
    auto op = us.undo();
    EXPECT_EQ(op.offset, 5);
    EXPECT_EQ(op.removedText, "World");
}

TEST(UndoStack, ReplaceOperation) {
    UndoStack us;
    us.push(0, "old", "new");
    auto op = us.undo();
    EXPECT_EQ(op.removedText, "old");
    EXPECT_EQ(op.addedText, "new");
}
