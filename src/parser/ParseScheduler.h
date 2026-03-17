#pragma once

#include <QObject>

class ParseScheduler : public QObject {
    Q_OBJECT
public:
    explicit ParseScheduler(QObject* parent = nullptr);
    ~ParseScheduler() override;
};
