#pragma once

#include <QApplication>

class Application final : public QApplication {
    Q_OBJECT

public:
    Application(int &argc, char **argv);

    void initialize();

signals:
    void aboutToQuitCleanly();
};
