#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QListWidget>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>

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

protected:
    void dragEnterEvent(QDragEnterEvent *event);
    void dropEvent(QDropEvent *event);

private slots:
    void on_EngineVersionSelector_currentIndexChanged(int index);

private:
    Ui::MainWindow *ui;

    QList<UnrealInstall> GetEngineInstalls();

    UnrealInstall SelectedUnrealInstallation;

    QList<UnrealInstall> UnrealInstallations;
};

#endif // MAINWINDOW_H
