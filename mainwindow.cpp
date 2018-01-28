#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QStandardPaths>
#include <QtDebug>
#include <QDirIterator>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>

#include <QMimeData>

#include <QProcess>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setAcceptDrops(true);

    UnrealInstallations = GetEngineInstalls();

    int i = 0;
    for (UnrealInstall EngineInstall : UnrealInstallations)
    {
        ui->EngineVersionSelector->addItem(EngineInstall.GetName(), QVariant(i));

        i++;
    }
}

QList<UnrealInstall> MainWindow::GetEngineInstalls()
{
    // Get the ue4 versions
    QList<UnrealInstall> UnrealInstalls;

    // Fetch the binary install locations (for windows).

#ifdef Q_OS_WIN

    QStringList paths = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    QString ProgramDataPath;

    for (QString path : paths)
    {
        // Assuming that the *real* one is the only one ending in ProgramData
        if (path.endsWith("ProgramData"))
        {
            ProgramDataPath = path;
            break;
        }

#ifdef QT_DEBUG
    qDebug() << "Found GenericDataLocation Path: " << path;
#endif

    }
    QString LauncherInstalledPath = ProgramDataPath + "/Epic/UnrealEngineLauncher/LauncherInstalled.dat";

    QFile LauncherInstalled(LauncherInstalledPath);

    if (!LauncherInstalled.open(QFile::ReadOnly | QFile::Text))
    {
#ifdef QT_DEBUG
        qDebug() << "Unable To Open LauncherInstalled.dat Engine Configuration File....Skipping Automatic Engine Detection. Path: " << LauncherInstalledPath;
#endif

        return UnrealInstalls;

    }
    QTextStream LauncherInstalledTS(&LauncherInstalled);
    QString LauncherInstalledText = LauncherInstalledTS.readAll();

    QJsonObject jLauncherInstalled = QJsonDocument::fromJson(LauncherInstalledText.toUtf8()).object();

    if (!jLauncherInstalled.contains("InstallationList") && jLauncherInstalled["InstallationList"].isArray())
    {
#ifdef QT_DEBUG
         qDebug() << "Invalid LauncherInstalled.dat Engine Configuration File....Skipping Automatic Engine Detection";
#endif

         return UnrealInstalls;
    }

    for (QJsonValue EngineInstallVal : jLauncherInstalled["InstallationList"].toArray())
    {
        if (!EngineInstallVal.isObject())
        {
#ifdef QT_DEBUG
         qDebug() << "Invalid Launcher Install....skipping this one!";
         continue;
#endif
        }

        QJsonObject EngineInstall = EngineInstallVal.toObject();

        QString EngineLocation = EngineInstall["InstallLocation"].toString();
        QString EngineName = EngineInstall["AppName"].toString();

        // Only get the ue4 builds - filter out the plugins  (that'll be formatted like ConfigBPPlugin_4.17
        if (EngineName.startsWith("UE_"))
        {
            // Add this engine version to the unreal engine installs list
            UnrealInstalls.append(UnrealInstall(EngineName, EngineLocation));

#ifdef QT_DEBUG
            qDebug() << "Found Engine Version: {Name=" << EngineName << ";Location=" << EngineLocation << "}";
#endif
        }

    }

#endif

    // Fetch any custom ue4 install paths the user may have added

    // Open up uPIT's settings file
    QSettings Settings("HowToCompute", "uPBT");

    // Create a quick list to add the custom installs to (so they can be added seperate of the *proper* list in case anything goes wrong)
    QList<UnrealInstall> CustomInstalls;

    // Read the Custom Unreal Engine Installs array from the settings object.
    int size = Settings.beginReadArray("CustomUnrealEngineInstalls");
    for (int i = 0; i < size; ++i) {
        // Get this element out of the settings
        Settings.setArrayIndex(i);

        // Extract the installation's name & path
        QString InstallName = Settings.value("Name").toString();
        QString InstallPath = Settings.value("Path").toString();

        // Create an UnrealInstall based on the name & path, and add it to the Custom Installs list.
        CustomInstalls.append(UnrealInstall(InstallName, InstallPath));
    }

    // Done reading, so "close" the array.
    Settings.endArray();

    // Add the list of custom installs to the list of (binary) Unreal Engine installations.
    UnrealInstalls += CustomInstalls;

    // Return the final list of UE4 binary installs & custom installs.
    return UnrealInstalls;
}

void MainWindow::BuildPlugin(QString PluginPath)
{
    QString RunUATPath = SelectedUnrealInstallation.GetPath() + "/Engine/Build/BatchFiles/RunUAT.bat";
    QStringList RunUATFlags;
    RunUATFlags << "BuildPlugin";
    RunUATFlags << "-Plugin=" + PluginPath;

    // Get or create a path where to package the plugin
    QDir PackageLocation = QDir("D:/PluginBuilds/testing_qt/plugin");// TODO QStandardPaths::standardLocations(QStandardPaths::DataLocation)[0] + "/BuiltPlugins/" + selectedPlugin.GetName() + "-" + SelectedUnrealInstallation.GetName();

    if (!PackageLocation.exists())
    {
        // Create the directory (use a default constructor due to the way mdkir works)
        QDir().mkdir(PackageLocation.path());
    }

    RunUATFlags << "-Package=" + PackageLocation.path();
    RunUATFlags << "-Rocket";

#ifdef QT_DEBUG
    qDebug() << "Going to run " << RunUATPath << " with the flags: " << RunUATFlags << " to build this plugin...";
#endif

    // Run the UAT and wait until it's finished
    BuildProcess = new QProcess(this);

    connect(BuildProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),this, &MainWindow::on_PluginBuild_complete);
    BuildProcess->start(RunUATPath, RunUATFlags);

    // Set the progress bar to 50% to tell the user the build is in progress
    ui->progressBar->setValue(50);

    // Return so the async callback can do it's thing and we don't copy the files twice.
    return;
}

bool MainWindow::on_PluginBuild_complete(int exitCode, QProcess::ExitStatus exitStatus)
{
    QString OutputLog = QString(BuildProcess->readAll());

    qDebug() << OutputLog;

    if (exitStatus == QProcess::NormalExit && exitCode == 0)
    {
#ifdef QT_DEBUG
        qDebug() << "Successfully Built Plugin Binaries.";
#endif
    }
    else
    {
#ifdef QT_DEBUG
        qDebug() << "Finished Building Plugin Binaries, But Failed. Output Log:";
        //qDebug() <<  QString(p.readAll()); TODO find a way to still report this
#endif

        // If not, ask the user whether or not they want to rebuild it
        QMessageBox BuildFailedPrompt;
        BuildFailedPrompt.setWindowTitle("Failed To Build Plugin Binaries");
        BuildFailedPrompt.setText("There Was An Issue Building Your Plugin's Binaries. Would You Still Like To Continue On With The Installation?");
        BuildFailedPrompt.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);

        int UserResponse = BuildFailedPrompt.exec();

        // Which button did the user press?
        switch (UserResponse)
        {
            case QMessageBox::Ok:
                break;
            case QMessageBox::Cancel:
                // Reset the progress bar & return so the rest doesn't get executed
                ui->progressBar->setEnabled(false);
                ui->progressBar->setValue(0);
                return false;
                break;
        }
    }

    ui->progressBar->setValue(75);

    return false;
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->urls().length() > 1)
    {
        // Too many files dragged - can only accept one at a time!
        return;
    }
    // TODO - Check if it isn't already compiling another plugin (in which case it should reject)

    if (QFileInfo(event->mimeData()->urls().at(0).toLocalFile()).suffix() == "uplugin")
    {
        event->acceptProposedAction();
        //qDebug() << event->mimeData()->urls();
    }
    else
    {
        qDebug() << "Not Accepting: " << QFileInfo(event->mimeData()->urls().at(0).toLocalFile()).suffix();
    }

   // if (event->mimeData()->hasFormat("text/plain"))
   //     event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    qDebug() << "Going to build plugin @ " << event->mimeData()->urls().at(0).toLocalFile() << "...";

    if (BuildProcess && !BuildProcess->atEnd())
    {
        // There is still a build process going on - can't build!
        // TODO a handler/dialog to tell the user about this.
    }
    else
    {
        // Attempt to build the plugin!
        event->acceptProposedAction();
        BuildPlugin(event->mimeData()->urls().at(0).toLocalFile());
    }
}

void MainWindow::on_EngineVersionSelector_currentIndexChanged(int index)
{
    // Use the list's index we got when we where adding these to get the right engine version
    UnrealInstall UnrealInstallation = UnrealInstallations[ui->EngineVersionSelector->itemData(index).toInt()];

    SelectedUnrealInstallation = UnrealInstallation;

#ifdef QT_DEBUG
    qDebug() << "Unreal Version Switched To {Name=" << UnrealInstallation.GetName() << ";Path=" << UnrealInstallation.GetPath() << "}";
#endif
}

MainWindow::~MainWindow()
{
    delete ui;
}
