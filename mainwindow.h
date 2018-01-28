#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QListWidget>

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>

#include <QProcess>

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

    void BuildPlugin(QString PluginPath);

    bool on_PluginBuild_complete(int exitCode, QProcess::ExitStatus exitStatus);

    UnrealInstall SelectedUnrealInstallation;

    QList<UnrealInstall> UnrealInstallations;

    // This hack sucks - but it's the only real way to get the output log of the build command to the completion/failure handler
    QProcess *BuildProcess;

    bool bIsBuilding = false;

    // The format string (either default or read from config) to use when deciding where to build a plugin to.
    QString BuildTargetFormat;

    // The directory into which the currently building plugin is being built.
    QString BuildTarget;

};

#endif // MAINWINDOW_H
