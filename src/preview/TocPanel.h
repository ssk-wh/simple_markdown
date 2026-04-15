// src/preview/TocPanel.h
//
// Spec: specs/模块-preview/07-TOC面板.md
// Invariants: INV-TOC-COLLAPSE, INV-TOC-WIDTH-AUTO, INV-TOC-VISUAL, INV-TOC-THEME-ONLY
#pragma once

#include <QWidget>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QSet>
#include <QMap>
#include <QDateTime>
#include "Theme.h"

class QScrollArea;
class QVBoxLayout;
class QLabel;
class QToolButton;
class QFrame;

struct TocEntry {
    QString title;
    int level = 1;
    int sourceLine = 0;
};

// [INV-TOC-DOCCARD-NO-REPARSE]
// 文档摘要信息；由 MainWindow 在已有 updateStatusBarStats 流程中同步计算并推送。
// TocPanel 自身不 parse markdown。
struct DocInfo {
    int wordCount = 0;
    int charCount = 0;          // 含空白
    int charCountNoSpace = 0;
    int lineCount = 0;
    qint64 sizeBytes = -1;      // -1 表示未保存
    QDateTime mtime;            // 无效表示未保存
    QString frontmatterTitle;   // 空则不显示
    QStringList frontmatterTags;
};

class TocPanel : public QWidget {
    Q_OBJECT
public:
    explicit TocPanel(QWidget* parent = nullptr);

    void setEntries(const QVector<TocEntry>& entries);
    void setTheme(const Theme& theme);
    void setHighlightedEntries(const QSet<int>& indices);

    // [INV-TOC-COLLAPSE] 当前文件路径 → 折叠状态持久化键
    // 空串表示未命名文件，不持久化
    void setCurrentFileKey(const QString& key);

    // [INV-TOC-DOCCARD-NO-REPARSE] 推送已由 MainWindow 算好的文档摘要
    void setDocumentInfo(const DocInfo& info);
    // [INV-TOC-DOCCARD-COLLAPSE-PERSIST] 信息卡折叠态
    bool isDocCardCollapsed() const { return m_docCardCollapsed; }
    void setDocCardCollapsed(bool v);

    const QVector<TocEntry>& entries() const { return m_entries; }
    const QSet<int>& highlightedEntries() const { return m_highlightedEntries; }

    // [INV-TOC-WIDTH-AUTO] 按可见条目测算偏好宽度
    int preferredWidth() const;

    // ---- 测试/诊断用 ----
    // 暴露内部折叠相关状态，单元测试 TocPanelTest 使用。
    // （非 public API，应用代码不应依赖）
    const QVector<int>& parentIndexForTest() const { return m_parentIndex; }
    const QVector<bool>& visibleForTest() const { return m_visible; }
    bool isEntryVisibleForTest(int idx) const {
        return idx >= 0 && idx < m_visible.size() && m_visible[idx];
    }
    bool hasChildrenForTest(int idx) const { return hasChildren(idx); }
    void toggleCollapseForTest(int idx) { toggleCollapse(idx); }

signals:
    void headingClicked(int sourceLine);
    void preferredWidthChanged(int widthPx);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void buildList();
    void recomputeVisibility();
    bool isChildOf(int parentIdx, int childIdx) const;
    bool hasChildren(int idx) const;
    void toggleCollapse(int idx);

    // 持久化辅助
    QString collapseSettingsKey(const QString& fileKey) const;
    void loadCollapsed();
    void saveCollapsed();

    QScrollArea* m_scrollArea;
    QVBoxLayout* m_listLayout;

    // DocInfoCard 相关
    QFrame* m_docCard = nullptr;
    QToolButton* m_docCardToggle = nullptr;
    QLabel* m_docCardBody = nullptr;
    DocInfo m_docInfo;
    bool m_docCardCollapsed = false;
    void rebuildDocCard();
    void applyDocCardTheme();

    QVector<TocEntry> m_entries;                     // 原始扁平条目
    QVector<int> m_parentIndex;                      // entry i 的父 entry index (-1 若无)
    QVector<bool> m_hasChild;                        // entry i 是否有任何后代
    QVector<bool> m_visible;                         // entry i 是否应显示（受折叠影响）
    QSet<int> m_collapsed;                           // 当前被折叠的 parent index 集合
    QString m_currentFileKey;                        // 持久化 key（空串不保存）

    Theme m_theme;
    QSet<int> m_highlightedEntries;
    int m_focusIdx = -1;                             // 键盘焦点条目（支持 ← → Enter）
};
