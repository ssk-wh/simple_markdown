#include <QApplication>
#include <QMainWindow>
#include "EditorWidget.h"
#include "Document.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle("EasyMarkdown");
    window.resize(1024, 768);

    auto* editor = new EditorWidget(&window);
    window.setCentralWidget(editor);

    // Load sample text or file from command line
    if (argc > 1) {
        editor->document()->loadFromFile(QString::fromLocal8Bit(argv[1]));
    } else {
        editor->document()->insert(0,
            "# EasyMarkdown\n"
            "\n"
            "This is a **lightweight** Markdown editor.\n"
            "\n"
            "## Features\n"
            "\n"
            "- Fast startup (< 0.5s)\n"
            "- Low memory (< 30MB)\n"
            "- Custom-drawn editor\n"
            "- Real-time preview\n"
            "\n"
            "```cpp\n"
            "int main() {\n"
            "    return 0;\n"
            "}\n"
            "```\n"
            "\n"
            "| Column A | Column B |\n"
            "|----------|----------|\n"
            "| Data 1   | Data 2   |\n"
        );
    }

    window.show();
    return app.exec();
}
