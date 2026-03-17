#include <QApplication>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("EasyMarkdown");
    app.setOrganizationName("EasyMarkdown");

    MainWindow window;

    if (argc > 1) {
        window.openFile(QString::fromLocal8Bit(argv[1]));
    } else {
        window.newTab();
    }

    window.show();
    return app.exec();
}
