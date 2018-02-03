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
#include <QInputDialog>
#include <QFileDialog>

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

    // Open up uPBT's settings file
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

void MainWindow::on_actionAdd_Unreal_Engine_Install_triggered()
{
    // NOTE: Assume users will go into the root (eg .../UE_4.17/ and not .../UE_4.17/Engine/ or something like that)

    QString InstallDirectory = QFileDialog::getExistingDirectory(this, "Open The Engine Install", "");

#ifdef QT_DEBUG
    qDebug() << "User Selected Installation Directory: " << InstallDirectory;
#endif

    if (InstallDirectory.isEmpty())
    {
        // The install directory was blank, meaning that the user (probably) canceled the dialog. Don't continue on, but don't show an error either.
        return;
    }
    else if (!QDir(InstallDirectory).exists())
    {
#ifdef QT_DEBUG
        qDebug() << "Selected Install Directory Does Not Exist!";
#endif
        // This literally shouldn't ever happen, so just drop the request.
        return;
    }

    bool bGotName;
    QString EngineName = QInputDialog::getText(this, "Add Custom UE4 Installation", "Please Give This Installation A (Descriptive) Name.", QLineEdit::Normal, "CUSTOM_UNREAL_INSTALL", &bGotName);

    if (!bGotName)
    {
        // The user pressed the cancel button, so don't actually add the engine (assume they don't want to add the engine anymore).
        return;
    }

    if (EngineName.isEmpty())
    {
        // The user entered a blank name, which isn't allowed, so don't add the engine and prompt them to retry.
        QMessageBox ErrorPrompt;
        ErrorPrompt.setWindowTitle("Uh Oh!");
        ErrorPrompt.setText("The Name You Entered Was Unfortionately Invalid (Blank Name). Please Try Again.");
        ErrorPrompt.setStandardButtons(QMessageBox::Ok);
        ErrorPrompt.exec();
        return;
    }

    // Add this newly created custom installation to the list of custom ue4 installations.

    QSettings Settings("HowToCompute", "uPBT");

    UnrealInstall CustomInstall(EngineName, InstallDirectory);

    // Get the number of items in the array.
    int size = Settings.beginReadArray("CustomUnrealEngineInstalls");
    Settings.endArray();

    // Append this install to the engine's custom installs list.
    Settings.beginWriteArray("CustomUnrealEngineInstalls");
    Settings.setArrayIndex(size);
    Settings.setValue("Name", CustomInstall.GetName());
    Settings.setValue("Path", CustomInstall.GetPath());
    Settings.endArray();

    // Add the new install to the (known) unreal installs list & add it to the UI dropdown.
    UnrealInstallations.append(CustomInstall);

    ui->EngineVersionSelector->addItem(CustomInstall.GetName(), QVariant(ui->EngineVersionSelector->count()));
}

void MainWindow::on_actionRemove_Unreal_Engine_Install_triggered()
{
    QInputDialog qDialog ;

    // Get all of the engines' names.
    QStringList items;
    for (UnrealInstall Installation : UnrealInstallations)
    {
        // Though adding them by name may not be the absolute best option out there - it would save having to create a new dialog just for this purpose.
        items.append(Installation.GetName());
    }

    // Create a combobox dialog that allows the user to select which version to remove.
    qDialog.setOptions(QInputDialog::UseListViewForComboBoxItems);
    qDialog.setComboBoxItems(items);
    qDialog.setWindowTitle("Choose action");

    // Run the dialog, and if it succeeds, get the returned name & remove it.
    if(qDialog.exec())
    {
        QString SelectedEngineName = qDialog.textValue();

        if (SelectedEngineName.length() <= 0)
        {
#ifdef QT_DEBUG
            qDebug() << "Selected invalid engine version: " << SelectedEngineName;
#endif
        }

        // TODO: Make the user only be able to select the custom installs (AKA read the settings)

        // Otherwise simply remove the engine version from the configuration & currently loaded engine versions, and return out.
        for (UnrealInstall Installation : UnrealInstallations)
        {
            if (Installation.GetName() == SelectedEngineName)
            {
                QSettings Settings("HowToCompute", "uPIT");

                QList<UnrealInstall> ConfigurationInstalls;

                // Read the array so we can remove the correct one.
                int size = Settings.beginReadArray("CustomUnrealEngineInstalls");

                for (int i = 0; i < size; ++i) {
                    // Get this element out of the settings
                    Settings.setArrayIndex(i);

                    // Extract the installation's name & path
                    QString InstallName = Settings.value("Name").toString();
                    QString InstallPath = Settings.value("Path").toString();

                    // Ommit the selected engine
                    if (InstallName != SelectedEngineName)
                    {
                        // Create an UnrealInstall based on the name & path, and add it to the Custom Installs list.
                        ConfigurationInstalls.append(UnrealInstall(InstallName, InstallPath));
                    }
                }
                Settings.endArray();

                // Remove/clear the array from the settings
                Settings.remove("CustomUnrealEngineInstalls");

                // And rewrite the updated array that ommits the removed engine version
                Settings.beginWriteArray("CustomUnrealEngineInstalls");
                for (UnrealInstall SInstallation : ConfigurationInstalls)
                {
                    Settings.setValue("Name", SInstallation.GetName());
                    Settings.setValue("Path", SInstallation.GetPath());
                }
                Settings.endArray();

                // Remove the install from the currently enabled list, and refresh the installs by reloading them. (or removing them - TBD this needs to be implemented)
                UnrealInstallations.removeOne(Installation);
                // TODO ui->EngineVersionSelector->removeItem(ui->EngineVersionSelector->chi);
            }
        }
    }
    else
    {
        // TODO - error occured
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}
