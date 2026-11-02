#include "ChangelogDialog.h"
#include "ViewerWidget.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QFile>
#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QLabel>

ChangelogDialog::ChangelogDialog(const Theme& theme, QWidget* parent)
    : QDialog(parent), m_viewer(nullptr)
{
    setWindowTitle(tr("Update History"));
    setMinimumSize(700, 500);

    // 查看器组件（2.0.0 起与主窗口共用同一套只读渲染管线）
    m_viewer = new viewer::ViewerWidget(this);
    m_viewer->setTheme(theme);

    // 关闭按钮：与 onShowWelcome 首次启动欢迎对话框完全一致的最简实现
    // [Spec 模块-app/07-快捷键弹窗.md INV-6] 不 setStyleSheet / setObjectName /
    // setSizePolicy / setDefault：颜色与尺寸完全靠 MainWindow::applyTheme 的
    // 全局 "QDialog QPushButton {...}" cascade 决定。
    QPushButton* closeBtn = new QPushButton(tr("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->addWidget(m_viewer);
    layout->addWidget(closeBtn);

    loadChangelog();
}

void ChangelogDialog::loadChangelog()
{
    // 搜索优先级：应用目录 > 相对开发目录 > 系统安装路径
    QStringList paths;

    // Windows / Linux：应用同目录
    paths << QApplication::applicationDirPath() + "/CHANGELOG.md";

    // 开发环境：项目根目录（从 app/ 目录算起）
    paths << QApplication::applicationDirPath() + "/../CHANGELOG.md";
    paths << QApplication::applicationDirPath() + "/../../CHANGELOG.md";

    // Linux：系统路径
#ifdef Q_OS_LINUX
    paths << "/usr/share/doc/simplemarkdown/CHANGELOG.md";
    paths << "/usr/local/share/doc/simplemarkdown/CHANGELOG.md";
#endif

    for (const auto& path : paths) {
        if (QFileInfo::exists(path)) {
            qDebug() << "[ChangelogDialog] Loaded from:" << path;
            m_viewer->loadFile(QFileInfo(path).absoluteFilePath());
            return;
        }
    }

    // 未找到更新历史文件：隐藏查看器，改显示一条说明
    m_viewer->hide();
    auto* label = new QLabel(tr("Update history file not found.\n\n"
                                "Please ensure CHANGELOG.md is installed with the application."), this);
    label->setWordWrap(true);
    static_cast<QVBoxLayout*>(this->layout())->insertWidget(0, label);
}
