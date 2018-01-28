#ifndef BUILDERRORDIALOG_H
#define BUILDERRORDIALOG_H

#include <QDialog>

namespace Ui {
class BuildErrorDialog;
}

class BuildErrorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit BuildErrorDialog(QWidget *parent = 0);
    BuildErrorDialog(QWidget *parent, QString Error);
    ~BuildErrorDialog();

private:
    Ui::BuildErrorDialog *ui;
};

#endif // BUILDERRORDIALOG_H
