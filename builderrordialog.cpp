#include "builderrordialog.h"
#include "ui_builderrordialog.h"

BuildErrorDialog::BuildErrorDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::BuildErrorDialog)
{
    ui->setupUi(this);
}

BuildErrorDialog::BuildErrorDialog(QWidget *parent, QString Error) :
    BuildErrorDialog(parent)
{
    ui->errorText->setText(Error);
}

BuildErrorDialog::~BuildErrorDialog()
{
    delete ui;
}
