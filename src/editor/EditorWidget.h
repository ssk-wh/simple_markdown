#pragma once
#include <QAbstractScrollArea>
#include <QTimer>

class Document;
class EditorLayout;
class EditorPainter;
class EditorInput;
class GutterRenderer;

class EditorWidget : public QAbstractScrollArea {
    Q_OBJECT
public:
    explicit EditorWidget(QWidget* parent = nullptr);
    ~EditorWidget();

    void setDocument(Document* doc);
    Document* document() const;
    EditorLayout* editorLayout() const;

    int gutterWidth() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

private slots:
    void onTextChanged(int offset, int removedLen, int addedLen);

private:
    Document* m_doc = nullptr;
    EditorLayout* m_layout = nullptr;
    EditorPainter* m_painter = nullptr;
    int m_gutterWidth = 50;

    void updateScrollBars();
    void updateGutterWidth();
    int firstVisibleLine() const;
    int lastVisibleLine() const;
    qreal scrollY() const;
};
