#include "mainwindow.h"
#include "QClipboard"
#include "QFileDialog"
#include "QMessageBox"
#include <string>
// #include "opencc_fmmseg_capi.h"
#include "zhoutilities.h"
#include "draglistwidget.h"
// #include "OfficeConverter.hpp"
#include "OfficeConverterMinizip.hpp"

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), ui(new Ui::MainWindowClass())
{
    ui->setupUi(this);
    ui->tabWidget->setCurrentIndex(0);
    // openccInstance = opencc_new();
    // opencc_set_parallel(openccInstance, false);
    connect(ui->tbSource, &TextEditWidget::fileDropped, this,
            [this](const QString& path)
            {
                refreshFromSource();
                if (path.isEmpty())
                {
                    ui->statusBar->showMessage("Text contents dropped");
                }
            });
}

MainWindow::~MainWindow()
{
    // if (openccInstance != nullptr)
    // {
    //     opencc_delete(openccInstance);
    //     openccInstance = nullptr;
    // }
    delete ui;
}

void MainWindow::on_btnExit_clicked() { this->close(); }

void MainWindow::on_actionExit_triggered() { QApplication::quit(); }

void MainWindow::on_actionAbout_triggered()
{
    QMessageBox::about(this, "About",
                       "ZhoConverter version 1.0.0 (c) 2025 Laisuk Lai");
}

void MainWindow::update_tbSource_info(const int text_code) const
{
    switch (text_code)
    {
    case 2:
        ui->rbS2t->setChecked(true);
        ui->lblSourceCode->setText(u8"zh-Hans (简体)");
        break;
    case 1:
        ui->rbT2s->setChecked(true);
        ui->lblSourceCode->setText(u8"zh-Hant (繁体)");
        break;
    case -1:
        ui->lblSourceCode->setText(u8"unknown (未知)");
        break;
    default:
        ui->lblSourceCode->setText(u8"non-zho （其它）");
        break;
    }
    ui->lblFileName->setText(
        ui->tbSource->contentFilename.section("/", -1, -1));

    if (!ui->tbSource->contentFilename.isEmpty())
    {
        ui->statusBar->showMessage("File: " + ui->tbSource->contentFilename);
    }
}

QString MainWindow::getCurrentConfig() const
{
    QString config;
    if (ui->rbManual->isChecked())
    {
        config = ui->cbManual->currentText().split(' ').first();
    }
    else
    {
        config =
            ui->rbS2t->isChecked()
                ? (ui->rbStd->isChecked()
                       ? "s2t"
                       : (ui->rbHK->isChecked()
                              ? "s2hk"
                              : (ui->cbTWCN->isChecked()
                                     ? "s2twp"
                                     : "s2tw")))
                : (ui->rbStd->isChecked()
                       ? "t2s"
                       : (ui->rbHK->isChecked()
                              ? "hk2s"
                              : (ui->cbTWCN->isChecked()
                                     ? "tw2sp"
                                     : "tw2s")));
    }
    return config;
}

void MainWindow::displayFileList(const QStringList& files) const
{
    for (const QString& file : files)
    {
        // Check if the file path is not already in the list box
        if (!filePathExists(file))
        {
            ui->listSource->addItem(file);
        }
    }
}

bool MainWindow::filePathExists(const QString& file_path) const
{
    // Check if the file path is already in the list box
    for (int index = 0; index < ui->listSource->count(); ++index)
    {
        if (const QListWidgetItem* item = ui->listSource->item(index); item && item->text() == file_path)
        {
            return true;
        }
    }
    return false;
}

void MainWindow::on_tabWidget_currentChanged(const int index) const
{
    switch (index)
    {
    case 0:
        ui->btnOpenFile->setEnabled(true);
        ui->btnSaveAs->setEnabled(true);
        break;
    case 1:
        ui->btnOpenFile->setEnabled(false);
        ui->btnSaveAs->setEnabled(false);
        break;
    default:
        break;
    }
}

void MainWindow::on_rbStd_clicked() const
{
    ui->cbTWCN->setCheckState(Qt::Unchecked);
}

void MainWindow::on_rbHK_clicked() const { ui->cbTWCN->setCheckState(Qt::Unchecked); }

void MainWindow::on_rbZHTW_clicked() const { ui->cbTWCN->setCheckState(Qt::Checked); }

void MainWindow::on_cbTWCN_stateChanged(const int state) const
{
    if (state)
    {
        ui->rbZHTW->setChecked(true);
    }
}

void MainWindow::on_btnPaste_clicked() const
{
    if (QGuiApplication::clipboard()->text().isEmpty() ||
        QGuiApplication::clipboard()->text().isNull())
    {
        ui->statusBar->showMessage("Clipboard empty");
        return;
    }

    QString text;

    try
    {
        text = QGuiApplication::clipboard()->text();
        ui->tbSource->document()->setPlainText(text);
        ui->tbSource->contentFilename = "";
        ui->statusBar->showMessage("Clipboard contents pasted.");
    }
    catch (...)
    {
        ui->statusBar->showMessage("Clipboard error.");
        return;
    }
    const int text_code = openccFmmsegHelper.zhoCheck(text.toStdString());
    update_tbSource_info(text_code);
}

void MainWindow::on_btnProcess_clicked()
{
    const QString config = getCurrentConfig();
    openccFmmsegHelper.setConfig(config.toStdString());

    const bool is_punctuation = ui->cbPunctuation->isChecked();
    openccFmmsegHelper.setPunctuation(is_punctuation);

    if (const int tab = ui->tabWidget->currentIndex(); tab == 0)
    {
        main_process(config, is_punctuation);
    }
    else if (tab == 1)
    {
        batch_process(config, is_punctuation);
    }
} // on_btnProcess_clicked

// ----- single text conversion -----
void MainWindow::main_process(const QString& config, const bool is_punctuation) const
{
    const QString input = ui->tbSource->toPlainText();
    if (input.isEmpty())
    {
        ui->statusBar->showMessage("Source content is empty");
        return;
    }

    if (ui->rbManual->isChecked())
    {
        ui->lblDestinationCode->setText(ui->cbManual->currentText());
    }
    else if (!ui->lblSourceCode->text().contains("non"))
    {
        ui->lblDestinationCode->setText(
            ui->rbS2t->isChecked() ? u8"zh-Hant (繁体)" : u8"zh-Hans (简体)"
        );
    }
    else
    {
        ui->lblDestinationCode->setText(u8"non-zho （其它）");
    }

    const QByteArray inUtf8 = input.toUtf8();
    const QByteArray cfgUtf8 = config.toUtf8();

    QElapsedTimer timer;
    timer.start();

    const auto output = openccFmmsegHelper.convert(
        inUtf8.constData(),
        cfgUtf8.constData(),
        is_punctuation
    );

    const qint64 elapsedMs = timer.elapsed();

    ui->tbDestination->document()->clear();

    if (!output.data())
    {
        ui->statusBar->showMessage(
            QString("Conversion failed in %1 ms. (%2)").arg(elapsedMs).arg(config)
        );
        return;
    }

    ui->tbDestination->document()->setPlainText(QString::fromUtf8(output));
    ui->statusBar->showMessage(
        QString("Conversion completed in %1 ms. (%2)").arg(elapsedMs).arg(config)
    );
}

// ----- batch files conversion -----
void MainWindow::batch_process(const QString& config, const bool is_punctuation)
{
    if (ui->listSource->count() == 0)
    {
        ui->statusBar->showMessage("Nothing to convert: Empty file list.");
        return;
    }

    const QString out_dir = ui->lineEditDir->text();
    if (!QDir(out_dir).exists())
    {
        QMessageBox msg;
        msg.setWindowTitle("Attention");
        msg.setIcon(QMessageBox::Information);
        msg.setText("Invalid output directory.");
        msg.setInformativeText("Output directory:\n" + out_dir + "\n not found.");
        msg.exec();
        ui->lineEditDir->setFocus();
        ui->statusBar->showMessage("Invalid output directory.");
        return;
    }

    ui->tbPreview->clear();

    const QSet<QString> OFFICE_EXTENSIONS{
        "docx", "xlsx", "pptx", "odt", "ods", "odp", "epub"
    };

    for (int index = 0; index < ui->listSource->count(); ++index)
    {
        QString file_path = ui->listSource->item(index)->text();
        QString file_basename = QFileInfo(file_path).baseName();
        QString file_extension = QFileInfo(file_path).suffix();

        if (ui->actionConvertFilename->isChecked())
        {
            file_basename = QString::fromStdString(
                openccFmmsegHelper.convert(file_basename.toStdString())
            );
        }

        const QString output_file_name =
            out_dir + "/" + file_basename + "_" + config + "." + file_extension;

        if (OFFICE_EXTENSIONS.contains(file_extension.toLower()))
        {
            auto [success, message] = OfficeConverterMinizip::Convert(
                file_path.toStdString(),
                output_file_name.toStdString(),
                file_extension.toLower().toStdString(),
                openccFmmsegHelper,
                config.toStdString(),
                is_punctuation,
                /*keepFont=*/true
            );

            ui->tbPreview->appendPlainText(QString("%1: %2 -> %3")
                .arg(QString::number(index + 1),
                     output_file_name,
                     QString::fromStdString(message)));
            continue;
        }

        if (file_path == output_file_name)
        {
            ui->tbPreview->appendPlainText(
                QString("%1: %2 -> Skip: Output Path = Source Path.")
                .arg(QString::number(index + 1), output_file_name));
            continue;
        }

        if (!QFile(file_path).exists())
        {
            ui->tbPreview->appendPlainText(QString("%1: %2 -> File not found.")
                .arg(QString::number(index + 1), file_path));
            continue;
        }

        QFile input_file(file_path);
        if (!input_file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            ui->tbPreview->appendPlainText(
                QString("%1: %2 -> Skip: Not text file.")
                .arg(QString::number(index + 1), file_path));
            continue;
        }

        const QString input_text = QTextStream(&input_file).readAll();
        input_file.close();

        const auto converted_text =
            openccFmmsegHelper.convert(input_text.toStdString(),
                                       config.toStdString(),
                                       is_punctuation);

        QFile output_file(output_file_name);
        if (output_file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QTextStream out(&output_file);
            out << QString::fromStdString(converted_text);
            output_file.close();
            ui->tbPreview->appendPlainText(
                QString("%1: %2 -> Done.")
                .arg(QString::number(index + 1), output_file_name));
        }
        else
        {
            ui->tbPreview->appendPlainText(
                QString("%1: %2 -> Error writing to file.")
                .arg(QString::number(index + 1), output_file_name));
        }
    }

    ui->statusBar->showMessage("Process completed");
}

void MainWindow::on_btnCopy_clicked() const
{
    if (ui->tbDestination->document()->isEmpty())
    {
        ui->statusBar->showMessage("Destination content empty.");
        return;
    }

    try
    {
        QGuiApplication::clipboard()->setText(
            ui->tbDestination->document()->toPlainText());
    }
    catch (...)
    {
        ui->statusBar->showMessage("Clipboard error.");
        return;
    }
    ui->statusBar->showMessage("Destination contents copied to clipboard");
}

void MainWindow::on_btnOpenFile_clicked()
{
    const QString file_name = QFileDialog::getOpenFileName(
        this, tr("Open Text File"), ".",
        tr("Text Files (*.txt);;"
            "Subtitle Files (*.srt *.vtt *.ass *.ttml2 *.xml));;"
            "XML Files (*.xml *.ttml2);;"
            "All Files (*.*)"));
    if (file_name.isEmpty())
        return;

    QFile file(file_name);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&file);
    const QString file_content = in.readAll();
    file.close();

    ui->tbSource->document()->setPlainText(file_content);
    ui->tbSource->contentFilename = file_name;
    ui->statusBar->showMessage(QStringLiteral("File: %1").arg(file_name));
    const int text_code = ZhoCheck(file_content.toStdString());
    update_tbSource_info(text_code);
}

void MainWindow::on_btnSaveAs_clicked()
{
    const auto filename =
        QFileDialog::getSaveFileName(this, tr("Save Text File"), "./File.txt",
                                     tr("Text File (*.txt);;All Files (*.*)"));
    if (filename.isEmpty())
        return;

    QFile file(filename);

    // Open the file
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&file);
    out << ui->tbDestination->document()->toPlainText();
    ui->statusBar->showMessage(QStringLiteral("File saved: %1").arg(filename));

    file.close();
}

void MainWindow::refreshFromSource() const
{
    if (ui->tbSource->toPlainText().isEmpty())
        return;

    const int text_code =
        openccFmmsegHelper.zhoCheck(ui->tbSource->toPlainText().toStdString());
    update_tbSource_info(text_code);
}

void MainWindow::on_tbSource_textChanged() const
{
    // const QLocale locale;
    ui->lblCharCount->setText(
        QStringLiteral("[ %L1 chars ]")
        // .arg(locale.toString(ui->tbSource->document()->toPlainText().length())));
        .arg(ui->tbSource->document()->toPlainText().length()));
}

void MainWindow::on_btnAdd_clicked()
{
    QFileDialog file_dialog(this);
    file_dialog.setFileMode(QFileDialog::ExistingFiles);

    if (const QStringList files =
        QFileDialog::getOpenFileNames(this,
                                      "Open Files",
                                      "",
                                      "Text Files (*.txt);;"
                                      "Subtitle Files (*.srt *.vtt *.ass *.ttml2 *.xml);;"
                                      "Office Files (*.docx *.xlsx *.pptx *.odt *.ods *.odp *.epub);;"
                                      "All Files (*.*)"); !files.isEmpty())
    {
        displayFileList(files);
        ui->statusBar->showMessage("File(s) added.");
    }
}

void MainWindow::on_btnRemove_clicked() const
{
    if (QList<QListWidgetItem*> selected_items = ui->listSource->selectedItems(); !selected_items.isEmpty())
    {
        for (qsizetype i = selected_items.size() - 1; i >= 0; --i)
        {
            const QListWidgetItem* selected_item = selected_items[i];
            const int row = ui->listSource->row(selected_item);
            ui->listSource->takeItem(row);
            delete selected_item;
        }
        ui->statusBar->showMessage("File(s) removed.");
    }
}

void MainWindow::on_btnListClear_clicked() const
{
    ui->listSource->clear();
    ui->statusBar->showMessage("All entries cleared.");
}

void MainWindow::on_btnPreview_clicked() const
{
    if (QList<QListWidgetItem*> selected_items = ui->listSource->selectedItems(); !selected_items.isEmpty())
    {
        const QListWidgetItem* selected_item = selected_items[0];
        const QString file_path = selected_item->text();

        QFile file(file_path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            QTextStream in(&file);
            const QString contents = in.readAll();
            file.close();
            ui->tbPreview->setPlainText(contents);
            ui->statusBar->showMessage("Preview: " + file_path);
        }
        else
        {
            ui->tbPreview->clear();
            ui->statusBar->showMessage(file_path + ": Not a valid text file.");
        }
    }
}

void MainWindow::on_btnOutDir_clicked()
{
    QFileDialog file_dialog(this);
    file_dialog.setFileMode(QFileDialog::Directory);

    if (const QString directory = QFileDialog::getExistingDirectory(this, ""); !directory.isEmpty())
    {
        ui->lineEditDir->setText(directory);
        ui->statusBar->showMessage("Output directory set: " + directory);
    }
}

void MainWindow::on_btnPreviewClear_clicked() const
{
    ui->tbPreview->clear();
    ui->statusBar->showMessage("Preview contents cleared");
}

void MainWindow::on_btnClearTbSource_clicked() const
{
    ui->tbSource->clear();
    ui->lblSourceCode->setText("");
    ui->lblFileName->setText("");
    ui->statusBar->showMessage("Source contents cleared");
}

void MainWindow::on_btnClearTbDestination_clicked() const
{
    ui->tbDestination->clear();
    ui->lblDestinationCode->setText("");
    ui->statusBar->showMessage("Destination contents cleared");
}

void MainWindow::on_cbManual_activated() const
{
    ui->rbManual->setChecked(true);
}
