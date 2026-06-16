// tests/app/ReloadDialogI18nTest.cpp
//
// Spec: specs/横切关注点/60-国际化.md — INV-QMSGBOX-TR（标准按钮用自定义 tr 文案）
// Bug: plans/2026-06-16-文件外部修改弹窗未国际化.md
// 验收：T-7（中文 locale 下「文件外部修改」弹窗按钮显示中文）
//
// 背景：Qt5.12 QMessageBox 标准按钮取 QPlatformTheme context，而项目可用的 qt_zh_CN.qm
// 翻译在 QDialogButtonBox context，不匹配 → 标准 Yes/No 按钮在中文界面下回退英文（已实测）。
// 修复：文件外部修改弹窗改用自定义 tr("Reload") / tr("Keep Current") 按钮，文案受项目翻译
// 控制。本测试锁定这两个按钮文案在 zh_CN 翻译中存在且为预期中文（防漏翻译回归）。

#include <gtest/gtest.h>

#include <QApplication>
#include <QString>
#include <QTranslator>

namespace {

class QAppFixture : public ::testing::Environment {
public:
    void SetUp() override {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char arg0[] = "ReloadDialogI18nTest";
            static char* argv[] = {arg0, nullptr};
            app_ = new QApplication(argc, argv);
        }
    }
    void TearDown() override {}

private:
    QApplication* app_ = nullptr;
};

::testing::Environment* const g_env =
    ::testing::AddGlobalTestEnvironment(new QAppFixture);

}  // namespace

// T-7：文件外部修改弹窗的自定义按钮在中文翻译下显示中文。
TEST(ReloadDialogI18nTest, T7_ReloadButtonsTranslatedToChinese)
{
    QTranslator appTr;
    ASSERT_TRUE(appTr.load(QStringLiteral(":/translations/simple_markdown_zh_CN.qm")))
        << "simple_markdown_zh_CN.qm 未嵌入 qrc 或路径错误。";
    ASSERT_TRUE(qApp->installTranslator(&appTr));

    // tr("Reload") / tr("Keep Current") 在 MainWindow 成员函数内调用，context = MainWindow。
    const QString reload = QApplication::translate("MainWindow", "Reload");
    const QString keep = QApplication::translate("MainWindow", "Keep Current");

    EXPECT_EQ(reload, QString::fromUtf8("重新加载"))
        << "「Reload」未翻译为中文，实际: " << reload.toStdString()
        << "（检查 zh_CN.ts 是否有 MainWindow context 的 Reload 翻译且已 lrelease）";
    EXPECT_EQ(keep, QString::fromUtf8("保留当前"))
        << "「Keep Current」未翻译为中文，实际: " << keep.toStdString();

    qApp->removeTranslator(&appTr);
}
