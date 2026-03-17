#pragma once
#include <QAbstractScrollArea>
#include <QTimer>
#include <QInputMethodEvent>
#include "Selection.h"

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

    void ensureCursorVisible();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void keyPressEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

private slots:
    void onTextChanged(int offset, int removedLen, int addedLen);

private:
    Document* m_doc = nullptr;
    EditorLayout* m_layout = nullptr;
    EditorPainter* m_painter = nullptr;
    EditorInput* m_input = nullptr;
    int m_gutterWidth = 50;

    QTimer m_cursorBlinkTimer;
    bool m_cursorVisible = true;
    bool m_mousePressed = false;
    QString m_preeditString;

    TextPosition pixelToTextPosition(const QPoint& pos) const;

    void updateScrollBars();
    void updateGutterWidth();
    int firstVisibleLine() const;
    int lastVisibleLine() const;
    qreal scrollY() const;
};
