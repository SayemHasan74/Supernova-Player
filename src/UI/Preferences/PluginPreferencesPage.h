#pragma once

#include <QWidget>

class QLabel;
class QTableWidget;

class PluginPreferencesPage final : public QWidget {
    Q_OBJECT

public:
    explicit PluginPreferencesPage(QWidget *parent = nullptr);

private:
    void refresh();
    QTableWidget *m_table = nullptr;
    QLabel *m_errors = nullptr;
};

