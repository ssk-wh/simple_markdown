#pragma once
#include <QWidget>
#include "Theme.h"

class QLineEdit;
class QLabel;
class QHBoxLayout;
class QVBoxLayout;

// SearchBar 是 widget-agnostic 通用查找栏：所有交互通过 signal 暴露（findNext/Prev/
// searchTextChanged/closed/replaceNext/replaceAll），由调用方 widget 接信号自己实现。
// 编辑器和预览区共用。Spec: specs/模块-preview/11-预览区查找.md（解耦理由）
class SearchBar : public QWidget {
    Q_OBJECT
public:
    explicit SearchBar(QWidget* parent);

    void showSearch();      // Ctrl+F
    void showReplace();     // Ctrl+H
    void hideBar();

    QString searchText() const;
    void keepFocus();
    void setTheme(const Theme& theme);
    void updateMatchInfo(int currentIndex, int totalMatches);

    bool isCaseSensitive() const { return m_caseSensitive; }
    bool isWholeWord() const { return m_wholeWord; }
    bool isRegex() const { return m_regex; }

signals:
    void findNext(const QString& text);
    void findPrev(const QString& text);
    void replaceNext(const QString& find, const QString& replace);
    void replaceAll(const QString& find, const QString& replace);
    void searchTextChanged(const QString& text);
    void closed();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void applyThemeStyles();

    QLineEdit* m_findEdit;
    QLineEdit* m_replaceEdit;
    QWidget* m_replaceRow;
    QLabel* m_matchInfoLabel;

    // 搜索选项
    bool m_caseSensitive = false;
    bool m_wholeWord = false;
    bool m_regex = false;

    // 自绘按钮
    struct ToolButton {
        QRect rect;
        bool hovered = false;
        bool pressed = false;
    };
    ToolButton m_btnPrev;
    ToolButton m_btnNext;
    ToolButton m_btnClose;
    ToolButton m_btnReplace;
    ToolButton m_btnReplaceAll;
    ToolButton m_btnCaseSensitive;
    ToolButton m_btnWholeWord;
    ToolButton m_btnRegex;

    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void updateButtonRects();
    ToolButton* hitTest(const QPoint& pos);

    Theme m_theme;
    bool m_replaceVisible = false;
};
