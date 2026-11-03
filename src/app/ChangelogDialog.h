#pragma once

#include <QDialog>
#include "Theme.h"

namespace viewer { class ViewerWidget; }

class ChangelogDialog : public QDialog {
    Q_OBJECT
public:
    explicit ChangelogDialog(const Theme& theme, QWidget* parent = nullptr);

private:
    void loadChangelog();
    viewer::ViewerWidget* m_viewer = nullptr;
};
