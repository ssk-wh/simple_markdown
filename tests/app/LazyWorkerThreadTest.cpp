// tests/app/LazyWorkerThreadTest.cpp
//
// Spec: specs/横切关注点/10-线程模型.md INV-8
// 验收：T-5/T-6（构造不启动 worker，首次任务按需启动并正常退出）

#include <gtest/gtest.h>

#include <QApplication>
#include <QLineEdit>
#include <QTest>

#include "Document.h"
#include "EditorWidget.h"
#include "ParseScheduler.h"
#include "PreviewWidget.h"

namespace {

class QAppFixture : public ::testing::Environment {
public:
    void SetUp() override
    {
        if (!QApplication::instance()) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            static int argc = 1;
            static char arg0[] = "LazyWorkerThreadTest";
            static char* argv[] = {arg0, nullptr};
            app_ = new QApplication(argc, argv);
        }
    }

private:
    QApplication* app_ = nullptr;
};

::testing::Environment* const g_env =
    ::testing::AddGlobalTestEnvironment(new QAppFixture);

TEST(LazyWorkerThreadTest, T5_ConstructorsDoNotStartWorkers)
{
    EditorWidget editor;
    PreviewWidget preview;
    ParseScheduler parser;

    EXPECT_FALSE(editor.isSearchWorkerRunning());
    EXPECT_FALSE(preview.isSearchWorkerRunning());
    EXPECT_FALSE(parser.isWorkerRunning());
}

TEST(LazyWorkerThreadTest, T6_FirstTaskStartsWorkers)
{
    EditorWidget editor;
    PreviewWidget preview;
    ParseScheduler parser;
    Document document;

    editor.showSearchBar();
    auto* editorInput = editor.findChild<QLineEdit*>();
    ASSERT_NE(editorInput, nullptr);
    editorInput->setText(QStringLiteral("needle"));

    preview.showSearchBar();
    auto* previewInput = preview.findChild<QLineEdit*>();
    ASSERT_NE(previewInput, nullptr);
    previewInput->setText(QStringLiteral("needle"));

    parser.setDocument(&document);
    parser.parseNow();

    QTRY_VERIFY_WITH_TIMEOUT(editor.isSearchWorkerRunning(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(preview.isSearchWorkerRunning(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(parser.isWorkerRunning(), 1000);
}

}  // namespace
