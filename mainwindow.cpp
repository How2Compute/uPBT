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

#include "builderrordialog.h"

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


    // Open up uPBT's settings file
    QSettings Settings("HowToCompute", "uPBT");
    if (Settings.contains("PluginBuildPathFormat"))
    {
        // A custom variation has been set up  - use that one instead.
        BuildTargetFormat = Settings.value("PluginBuildPathFormat").toString();
    }
    else
    {
        // Use the default
        /// NOTE: Format: "/BuiltPlugins/<pluginName>/<pluginVersion>/<engine_version>"
        BuildTargetFormat = QStandardPaths::standardLocations(QStandardPaths::DataLocation)[0] + "/BuiltPlugins/%n/%v/%e";
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
    QFile PluginMeta(PluginPath);

    // Open the plugin to extract it's (friendly) name & version (name).
    /// NOTE: The above may be incorrect - however, this is how I believe most plugins do versioning & naming, and it is how I do it personally. Please open an issue if this causes huge issues for you!
    if (!PluginMeta.open(QFile::ReadOnly | QFile::Text))
    {
#ifdef QT_DEBUG
        qDebug() << "Error Opening UPlugin File to Generate Path & Build Plugin!";
#endif
        // Reset
        bIsBuilding = false;
        ui->progressBar->setValue(0);
        return;
    }

    // Parse the uplugin file as a JSON document so we can easily extract the required information
    QTextStream PluginMetaTS(&PluginMeta);
    QString PluginMetaString = PluginMetaTS.readAll();
    QJsonObject jPlugin = QJsonDocument::fromJson(PluginMetaString.toUtf8()).object();

    // Extract the plugin's name and version
    QString PluginName = jPlugin["FriendlyName"].toString();
    QString PluginVersion = jPlugin["VersionName"].toString();

    // Grab the engine's name for easy usage while formatting too
    QString EngineVersion = SelectedUnrealInstallation.GetName();

    // Copy over the format string so we can format it based on the plugin/selected engine.
    BuildTarget = BuildTargetFormat;

    // Replace the %n format specifier with the plugin's name if applicable
    if (BuildTargetFormat.contains("%n"))
    {
        BuildTarget.replace("%n", PluginName);
    }

    // Replace the %v format specifier with the plugin's version if applicable
    if (BuildTargetFormat.contains("%v"))
    {
        BuildTarget.replace("%v", PluginVersion);
    }

    // Replace the %e format specifier with the engine's name (usually version) if applicable
    if (BuildTargetFormat.contains("%e"))
    {
        BuildTarget.replace("%e", EngineVersion);
    }

    QString RunUATPath = SelectedUnrealInstallation.GetPath() + "/Engine/Build/BatchFiles/RunUAT.bat";
    QStringList RunUATFlags;
    RunUATFlags << "BuildPlugin";
    RunUATFlags << "-Plugin=" + PluginPath;

    // Get or create a path where to package the plugin
    QDir PackageLocation = QDir(BuildTarget);

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
        qDebug() << "Successfully Built Plugin.";
#endif
        // We have successfully build the plugin!
        ui->progressBar->setValue(100);

        // Create a dialog telling the user the plugin was successfully compiles/built.
        QMessageBox BuildSucceededDialog;
        BuildSucceededDialog.setWindowTitle("Succeeded!");
        BuildSucceededDialog.setTextFormat(Qt::RichText);
        BuildSucceededDialog.setText(QString("We successfully built that plugin! Output: <a href=\"file://%1\">%1</a>").arg(BuildTarget));
        BuildSucceededDialog.setStandardButtons(QMessageBox::Ok);
        BuildSucceededDialog.exec();
    }
    else
    {
#ifdef QT_DEBUG
        qDebug() << "Finished Building Plugin Binaries, But Failed.";
#endif
        // This could be hit if the application is being shut down - so check if the UI window is still valid to avoid issues like segfaults.
        if (ui)
        {
            // Changing the progress bar's state -> color is not the right thing to do with an error, so reset it to 0 and show an error dialog.
            ui->progressBar->setValue(0);

            // Create an error dialog that tells the user about the error that has happened (includes the output log).
            BuildErrorDialog dialog(this, OutputLog);
            dialog.setModal(true);
            dialog.exec();
        }
        else
        {
            // Be sure to just return as quickly as possible to avoid any potential issues - don't need to clean up as it's shutting down anyway and GC will do the job for us.
            return false;
        }
    }

    // Reset everything!
    ui->progressBar->setValue(0);
    BuildProcess->deleteLater();
    bIsBuilding = false;

    return true;
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

    if (bIsBuilding)
    {
        // There is still a build process going on - can't build!
        // TODO a handler/dialog to tell the user about this.
    }
    else
    {
        // Attempt to build the plugin!
        event->acceptProposedAction();
        bIsBuilding = true;
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
