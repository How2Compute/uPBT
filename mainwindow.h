#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QListWidget>
#include "unrealinstall.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    dropEvent(QDropEvent* event);

private:
    Ui::MainWindow *ui;

    QList<UnrealInstall> GetEngineInstalls();

    UnrealInstall SelectedUnrealInstallation;

    QList<UnrealInstall> UnrealInstallations;
};

#endif // MAINWINDOW_H
