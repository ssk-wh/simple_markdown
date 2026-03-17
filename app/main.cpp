#include <QApplication>
#include <QMainWindow>
#include <QSplitter>
#include "EditorWidget.h"
#include "PreviewWidget.h"
#include "ParseScheduler.h"
#include "Document.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    window.setWindowTitle("EasyMarkdown");
    window.resize(1280, 800);

    // 分屏：左编辑器 + 右预览
    auto* splitter = new QSplitter(Qt::Horizontal, &window);

    auto* editor = new EditorWidget(splitter);
    auto* preview = new PreviewWidget(splitter);

    splitter->addWidget(editor);
    splitter->addWidget(preview);
    splitter->setSizes({640, 640});

    window.setCentralWidget(splitter);

    // 解析调度器：连接编辑器到预览
    auto* scheduler = new ParseScheduler(&window);
    scheduler->setDocument(editor->document());
    QObject::connect(scheduler, &ParseScheduler::astReady,
                     preview, &PreviewWidget::updateAst);

    // 加载文件或示例文本
    if (argc > 1) {
        editor->document()->loadFromFile(QString::fromLocal8Bit(argv[1]));
    } else {
        editor->document()->insert(0,
            "# EasyMarkdown\n"
            "\n"
            "A **lightweight** cross-platform Markdown editor.\n"
            "\n"
            "## Features\n"
            "\n"
            "- Fast startup (< 0.5s)\n"
            "- Low memory (< 30MB)\n"
            "- Custom-drawn editor and preview\n"
            "- Real-time preview\n"
            "\n"
            "## Code Example\n"
            "\n"
            "```cpp\n"
            "#include <iostream>\n"
            "int main() {\n"
            "    std::cout << \"Hello!\" << std::endl;\n"
            "    return 0;\n"
            "}\n"
            "```\n"
            "\n"
            "> This is a blockquote.\n"
            "> It can span multiple lines.\n"
            "\n"
            "| Column A | Column B | Column C |\n"
            "|----------|----------|----------|\n"
            "| Data 1   | Data 2   | Data 3   |\n"
            "| Data 4   | Data 5   | Data 6   |\n"
            "\n"
            "---\n"
            "\n"
            "Visit [GitHub](https://github.com) for more info.\n"
            "\n"
            "*Italic text* and **bold text** and `inline code`.\n"
        );
    }

    // 触发初始解析
    scheduler->parseNow();

    window.show();
    return app.exec();
}
