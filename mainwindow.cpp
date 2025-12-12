#include "mainwindow.h"
#include "QClipboard"
#include "QFileDialog"
#include "QMessageBox"
#include <QThread>
#include <QTextDocumentFragment>
#include <string>
// #include "opencc_fmmseg_capi.h"
#include "zhoutilities.h"
#include "draglistwidget.h"
#include "filetype_utils.h"
#include "OfficeConverter.hpp"
#include "OfficeConverterMinizip.hpp"

// namespace {
//     inline const std::unordered_set<std::string> TEXTFILE_EXTENSIONS = {
//         "txt", "md", "rst",
//         "html", "htm", "xhtml", "xml",
//         "json", "yml", "yaml", "ini", "cfg", "toml",
//         "csv", "tsv",
//         "c", "cpp", "cc", "cxx", "h", "hpp",
//         "cs", "java", "kt", "kts",
//         "py", "rb", "go", "rs", "swift",
//         "js", "mjs", "cjs", "ts", "tsx", "jsx",
//         "sh", "bash", "zsh", "ps1", "cmd", "bat",
//         "gradle", "cmake", "make", "mak", "ninja",
//         "tex", "bib", "log",
//         "srt", "vtt", "ass", "ttml2"
//     };
//
//     inline const std::unordered_set<std::string> OFFICE_EXTENSIONS = {
//         "docx", "xlsx", "pptx", "odt", "ods", "odp", "epub"
//         // add more if needed: "doc","xls","ppt","rtf","csv" (csv also in text), etc.
//     };
//
//     bool isOfficeExt(const QString &extLower) {
//         return OFFICE_EXTENSIONS.count(extLower.toStdString()) != 0;
//     }
//
//     bool isTextExt(const QString &extLower) {
//         return TEXTFILE_EXTENSIONS.count(extLower.toStdString()) != 0;
//     }
//
//     bool isAllowedTextLike(const QString &extLower) {
//         // <- allow files with NO extension as text
//         return extLower.isEmpty() || isTextExt(extLower);
//     }
//
//     void appendLine(QPlainTextEdit *box, const QString &line) {
//         box->appendPlainText(line + QLatin1Char('\n'));
//     }
//
//     QString makeOutputPath(const QString &outDir,
//                            const QString &baseName,
//                            const QString &config,
//                            const QString &extLower) {
//         return QDir(outDir).filePath(baseName + "_" + config + (extLower.isEmpty() ? QString() : "." + extLower));
//     }
// } // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindowClass()) {
    ui->setupUi(this);
    ui->tabWidget->setCurrentIndex(0);
    // openccInstance = opencc_new();
    // opencc_set_parallel(openccInstance, false);
    connect(ui->tbSource, &TextEditWidget::fileDropped, this,
            [this](const QString &path) {
                refreshFromSource();
                if (path.isEmpty()) {
                    ui->statusBar->showMessage("Text contents dropped");
                }
            });

    connect(ui->tbSource, &TextEditWidget::pdfDropped, this,
            [this](const QString &path) {
                // Start PDF extraction in worker thread
                startPdfExtraction(path);
            });

    // --- Status-bar Cancel button ---
    m_cancelPdfButton = new QPushButton(tr("Cancel"), this);
    m_cancelPdfButton->setObjectName("btnCancelPdf");
    m_cancelPdfButton->setAutoDefault(false);
    m_cancelPdfButton->setFlat(true); // look like status-bar control
    m_cancelPdfButton->hide(); // hidden by default

    ui->statusBar->addPermanentWidget(m_cancelPdfButton);

    connect(m_cancelPdfButton, &QPushButton::clicked,
            this, &MainWindow::onCancelPdfClicked);
}

MainWindow::~MainWindow() {
    // if (openccInstance != nullptr)
    // {
    //     opencc_delete(openccInstance);
    //     openccInstance = nullptr;
    // }
    delete ui;
}

void MainWindow::on_btnExit_clicked() { this->close(); }

void MainWindow::on_actionExit_triggered() { QApplication::quit(); }

void MainWindow::on_actionAbout_triggered() {
    QMessageBox::about(this, "About",
                       "ZhoConverter version 1.0.0 (c) 2025 Laisuk Lai");
}

// MainWindow.cpp
void MainWindow::startPdfExtraction(const QString &filePath) {
    // Clean up any previous thread/worker if needed
    cleanupPdfThread();

    m_currentPdfFilePath = filePath; // <--- remember PDF path
    m_pdfThread = new QThread(this);
    m_pdfWorker = new PdfExtractWorker(filePath, /*addPdfPageHeader=*/ui->actionAddPageHeader->isChecked());

    m_pdfWorker->moveToThread(m_pdfThread);

    // When thread starts -> do work
    connect(m_pdfThread, &QThread::started,
            m_pdfWorker, &PdfExtractWorker::process);

    // Progress → update status bar text / emoji bar
    connect(m_pdfWorker, &PdfExtractWorker::progressChanged,
            this, [this](const int percent, const QString &bar) {
                ui->statusBar->showMessage(bar + "  " + QString::number(percent) + "%");
            });

    // Normal finish
    connect(m_pdfWorker, &PdfExtractWorker::finished,
            this, &MainWindow::onPdfExtractionFinished);

    // Cancelled
    connect(m_pdfWorker, &PdfExtractWorker::cancelled,
            this, &MainWindow::onPdfExtractionCancelled);

    // Error
    connect(m_pdfWorker, &PdfExtractWorker::errorOccurred,
            this, &MainWindow::onPdfExtractionError);

    // Cleanup when thread exits
    connect(m_pdfThread, &QThread::finished,
            m_pdfWorker, &QObject::deleteLater);
    connect(m_pdfThread, &QThread::finished,
            m_pdfThread, &QObject::deleteLater);

    // --- Show Cancel button while running ---
    m_cancelPdfButton->setEnabled(true);
    m_cancelPdfButton->show();

    m_pdfThread->start();
}

// void MainWindow::onCancelPdfClicked() const {
//     if (m_pdfWorker) {
//         // Direct call → runs immediately in GUI thread.
//         // requestCancel() only writes an atomic<bool>, which is thread-safe.
//         m_pdfWorker->requestCancel();
//
//         m_cancelPdfButton->setEnabled(false);
//         ui->statusBar->showMessage(tr("Cancelling PDF extraction..."));
//     }
// }
void MainWindow::onCancelPdfClicked() const
{
    if (m_pdfWorker) {
        // Existing PDF cancel logic
        // e.g. m_pdfWorker->requestCancel();
        ui->statusBar->showMessage("Cancelling PDF extraction...");
    } else if (m_batchWorker) {
        m_batchWorker->requestCancel();
        ui->statusBar->showMessage("Cancelling batch...");
    }
}

void MainWindow::onPdfExtractionFinished(const QString &text) {
    // Hide cancel button
    m_cancelPdfButton->hide();

    // Put extracted text into tbSource (Even if partially canceled,
    // but our worker only emits finished() when not cancelled)
    QString isReflow = "";
    if (!text.isEmpty()) {
        if (ui->actionAutoReflow->isChecked()) {
            isReflow = "(Reflowed)";
            // Convert to UTF-8 std::string
            const QByteArray utf8 = text.toUtf8();
            const std::string input(utf8.constData(),
                                    static_cast<std::size_t>(utf8.size()));

            const bool addPdfPageHeader = ui->actionAddPageHeader->isChecked();
            const bool compact = ui->actionCompactPdfText->isChecked();

            const std::string reflowed =
                    pdfium::ReflowCjkParagraphs(input, addPdfPageHeader, compact);

            // Back to QString
            const QString out = QString::fromUtf8(reflowed.c_str(),
                                                  static_cast<int>(reflowed.size()));

            ui->tbSource->document()->setPlainText(out);
        } else {
            ui->tbSource->document()->setPlainText(text);
        }

        ui->tbSource->contentFilename = m_currentPdfFilePath;

        // Run your language detection / info update
        const int text_code = ZhoCheck(text.toStdString());
        update_tbSource_info(text_code);
    }

    ui->statusBar->showMessage(
        tr("✅ PDF loaded %1: %2").arg(isReflow, m_currentPdfFilePath));

    cleanupPdfThread();

    m_currentPdfFilePath.clear();
}

void MainWindow::onPdfExtractionCancelled(const QString &partialText) {
    m_cancelPdfButton->hide();

    // Put partial text into tbSource
    if (!partialText.isEmpty()) {
        ui->tbSource->document()->setPlainText(partialText);
        ui->tbSource->contentFilename = m_currentPdfFilePath;

        const int text_code = ZhoCheck(partialText.toStdString());
        update_tbSource_info(text_code);
    }

    ui->statusBar->showMessage(
        tr("❌ PDF loading cancelled: %1").arg(m_currentPdfFilePath)
    );

    cleanupPdfThread();
    m_currentPdfFilePath.clear();
}


void MainWindow::onPdfExtractionError(const QString &message) {
    ui->statusBar->showMessage(tr("Error: %1").arg(message), 5000);
    m_cancelPdfButton->hide();

    cleanupPdfThread();
}

void MainWindow::cleanupPdfThread() {
    if (m_pdfThread) {
        m_pdfThread->quit(); // ask thread to stop event loop
        m_pdfThread->wait(); // block until fully stopped

        // deleteLater for worker & thread is already connected,
        // so we just reset pointers here.
        m_pdfThread = nullptr;
        m_pdfWorker = nullptr;
    }
}

// ------------------------------------
// Batch Slots
// ------------------------------------

void MainWindow::onBatchProgress(const int current, const int total) const {
    ui->statusBar->showMessage(
        QString("Processing %1/%2...").arg(current).arg(total));
}

void MainWindow::onBatchError(const QString &msg) const {
    ui->tbPreview->appendPlainText(QString("[Error] %1").arg(msg));
    ui->statusBar->showMessage(msg);

    m_cancelPdfButton->hide();
    // enableBatchUi(); // if you disable above
}

void MainWindow::onBatchFinished(const bool cancelled) const {
    if (cancelled) {
        ui->tbPreview->appendPlainText("❌ Batch cancelled.");
        ui->statusBar->showMessage("❌ Batch cancelled.");
    } else {
        ui->tbPreview->appendPlainText("✅ Batch conversion completed.");
        ui->statusBar->showMessage("Batch completed.");
    }

    m_cancelPdfButton->hide();
    // enableBatchUi(); // if you disable above
}

void MainWindow::onBatchThreadFinished() {
    m_batchThread = nullptr;
    m_batchWorker = nullptr;
}

void MainWindow::cleanupBatchThread() {
    if (m_batchThread) {
        m_batchThread->quit();
        m_batchThread->wait();
        m_batchThread = nullptr;
        m_batchWorker = nullptr;
    }
}


void MainWindow::update_tbSource_info(const int text_code) const {
    switch (text_code) {
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

    // if (!ui->tbSource->contentFilename.isEmpty())
    // {
    //     ui->statusBar->showMessage("File: " + ui->tbSource->contentFilename);
    // }
}

QString MainWindow::getCurrentConfig() const {
    QString config;
    if (ui->rbManual->isChecked()) {
        config = ui->cbManual->currentText().split(' ').first();
    } else {
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


void MainWindow::on_tabWidget_currentChanged(const int index) const {
    switch (index) {
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

void MainWindow::on_rbStd_clicked() const {
    ui->cbTWCN->setCheckState(Qt::Unchecked);
}

void MainWindow::on_rbHK_clicked() const { ui->cbTWCN->setCheckState(Qt::Unchecked); }

void MainWindow::on_rbZHTW_clicked() const { ui->cbTWCN->setCheckState(Qt::Checked); }

void MainWindow::on_cbTWCN_stateChanged(const int state) const {
    if (state) {
        ui->rbZHTW->setChecked(true);
    }
}

void MainWindow::on_btnPaste_clicked() const {
    if (QGuiApplication::clipboard()->text().isEmpty() ||
        QGuiApplication::clipboard()->text().isNull()) {
        ui->statusBar->showMessage("Clipboard empty");
        return;
    }

    QString text;

    try {
        text = QGuiApplication::clipboard()->text();
        ui->tbSource->document()->setPlainText(text);
        ui->tbSource->contentFilename = "";
        ui->statusBar->showMessage("Clipboard contents pasted.");
    } catch (...) {
        ui->statusBar->showMessage("Clipboard error.");
        return;
    }
    const int text_code = openccFmmsegHelper.zhoCheck(text.toStdString());
    update_tbSource_info(text_code);
}

void MainWindow::on_btnProcess_clicked() {
    const QString config = getCurrentConfig();
    openccFmmsegHelper.setConfig(config.toStdString());

    const bool is_punctuation = ui->cbPunctuation->isChecked();
    openccFmmsegHelper.setPunctuation(is_punctuation);

    if (const int tab = ui->tabWidget->currentIndex(); tab == 0) {
        main_process(config, is_punctuation);
    } else if (tab == 1) {
        // batch_process(config, is_punctuation);
        startBatchProcess(config, is_punctuation);
    }
} // on_btnProcess_clicked

// ----- single text conversion -----
void MainWindow::main_process(const QString &config, const bool is_punctuation) const {
    const QString input = ui->tbSource->toPlainText();
    if (input.isEmpty()) {
        ui->statusBar->showMessage("Source content is empty");
        return;
    }

    if (ui->rbManual->isChecked()) {
        ui->lblDestinationCode->setText(ui->cbManual->currentText());
    } else if (!ui->lblSourceCode->text().contains("non")) {
        ui->lblDestinationCode->setText(
            ui->rbS2t->isChecked() ? u8"zh-Hant (繁体)" : u8"zh-Hans (简体)"
        );
    } else {
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

    if (!output.data()) {
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

// ============================== batch process ==============================
// void MainWindow::batch_process(const QString &config, const bool is_punctuation) {
//     // ---- pre-checks
//     if (ui->listSource->count() == 0) {
//         ui->statusBar->showMessage("Nothing to convert: Empty file list.");
//         return;
//     }
//
//     const QString outDir = ui->lineEditDir->text();
//     if (!QDir(outDir).exists()) {
//         QMessageBox msg;
//         msg.setWindowTitle("Attention");
//         msg.setIcon(QMessageBox::Information);
//         msg.setText("Invalid output directory.");
//         msg.setInformativeText("Output directory:\n" + outDir + "\n not found.");
//         msg.exec();
//         ui->lineEditDir->setFocus();
//         ui->statusBar->showMessage("Invalid output directory.");
//         return;
//     }
//
//     ui->tbPreview->clear();
//
//     // Cache once
//     const std::string config_s = config.toStdString();
//
//     for (int i = 0; i < ui->listSource->count(); ++i) {
//         const QString srcPath = ui->listSource->item(i)->text();
//         const QFileInfo fi(srcPath);
//         const QString extLower = fi.suffix().toLower(); // normalized extension
//         const bool noExt = extLower.isEmpty();
//         QString baseName = fi.baseName(); // stem without last suffix
//
//         // Optional: convert filename stem (no punctuation for names unless you want it)
//         if (ui->actionConvertFilename->isChecked()) {
//             baseName = QString::fromStdString(
//                 openccFmmsegHelper.convert(baseName.toStdString(), config_s, /*punctuation=*/false)
//             );
//         }
//
//         const QString outPath = makeOutputPath(outDir, baseName, config, extLower);
//
//         // Same path? skip early
//         if (srcPath == outPath) {
//             appendLine(ui->tbPreview,
//                        QString("%1: %2 -> ❌ Skip: Output Path = Source Path.")
//                        .arg(QString::number(i + 1), outPath));
//             continue;
//         }
//
//         // Must exist
//         if (!fi.exists()) {
//             appendLine(ui->tbPreview,
//                        QString("%1: %2 -> ❌ File not found.")
//                        .arg(QString::number(i + 1), srcPath));
//             continue;
//         }
//
//         // ---- Office route
//         if (isOfficeExt(extLower)) {
//             auto [ok, msg] = OfficeConverterMinizip::Convert(
//                 srcPath.toStdString(),
//                 outPath.toStdString(),
//                 extLower.toStdString(),
//                 openccFmmsegHelper,
//                 config_s,
//                 is_punctuation,
//                 /*keepFont=*/true
//             );
//
//             appendLine(ui->tbPreview,
//                        QString("%1: %2 -> %3")
//                        .arg(QString::number(i + 1),
//                             outPath,
//                             QString::fromStdString(msg)));
//             continue;
//         }
//
//         // ---- Text-like route (includes NO extension)
//         if (!isAllowedTextLike(extLower)) {
//             appendLine(ui->tbPreview,
//                        QString("%1: %2 -> ❌ Skip: Unsupported file type.")
//                        .arg(QString::number(i + 1), srcPath));
//             continue;
//         }
//
//         QFile inFile(srcPath);
//         if (!inFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
//             appendLine(ui->tbPreview,
//                        QString("%1: %2 -> ❌ Error opening for read.")
//                        .arg(QString::number(i + 1), srcPath));
//             continue;
//         }
//         const QString inputText = QTextStream(&inFile).readAll();
//         inFile.close();
//
//         const std::string converted =
//                 openccFmmsegHelper.convert(inputText.toStdString(), config_s, is_punctuation);
//
//         QFile outFile(outPath);
//         if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
//             appendLine(ui->tbPreview,
//                        QString("%1: %2 -> ❌ Error opening for write: %3")
//                        .arg(QString::number(i + 1), outPath, outFile.errorString()));
//             continue;
//         } {
//             QTextStream ts(&outFile);
//             ts.setEncoding(QStringConverter::Utf8); // Qt 6
//             ts << QString::fromStdString(converted);
//         }
//         outFile.flush();
//         outFile.close();
//
//         // Success line (call out the no-extension case explicitly)
//         if (noExt) {
//             appendLine(ui->tbPreview,
//                        QString("%1: %2 -> ✅ Done (treated as text: no extension).")
//                        .arg(QString::number(i + 1), outPath));
//         } else {
//             appendLine(ui->tbPreview,
//                        QString("%1: %2 -> ✅ Done.")
//                        .arg(QString::number(i + 1), outPath));
//         }
//     }
//
//     ui->statusBar->showMessage("Batch process completed");
// }

void MainWindow::batch_process(const QString &config, const bool is_punctuation) {
    startBatchProcess(config, is_punctuation);
}


void MainWindow::startBatchProcess(const QString &config,
                                   const bool isPunctuation) {
    // ---- pre-checks (same as before)
    if (ui->listSource->count() == 0) {
        ui->statusBar->showMessage("Nothing to convert: Empty file list.");
        return;
    }

    const QString outDir = ui->lineEditDir->text();
    if (!QDir(outDir).exists()) {
        QMessageBox msg;
        msg.setWindowTitle("Attention");
        msg.setIcon(QMessageBox::Information);
        msg.setText("Invalid output directory.");
        msg.setInformativeText("Output directory:\n" + outDir + "\n not found.");
        msg.exec();
        ui->lineEditDir->setFocus();
        ui->statusBar->showMessage("Invalid output directory.");
        return;
    }

    // Collect file list from QListWidget
    QStringList files;
    files.reserve(ui->listSource->count());
    for (int i = 0; i < ui->listSource->count(); ++i) {
        files << ui->listSource->item(i)->text();
    }

    ui->tbPreview->clear();
    ui->statusBar->showMessage("Starting batch conversion...");

    // Optionally disable UI controls while running
    // disableBatchUi(); // implement as you like

    // Clean up any previous batch thread if needed
    cleanupBatchThread();

    m_batchThread = new QThread(this);
    m_batchWorker = new BatchWorker(
        files,
        outDir,
        &openccFmmsegHelper,
        config,
        isPunctuation,
        ui->actionConvertFilename->isChecked(), // same as Python
        ui->actionAddPageHeader->isChecked(),     // 是否加 === [Page x/y] ===
        ui->actionAutoReflow->isChecked(),     // 自動重排
        ui->actionCompactPdfText->isChecked(),    // 緊湊模式
        nullptr
    );

    m_batchWorker->moveToThread(m_batchThread);

    // Thread start → worker.process()
    connect(m_batchThread, &QThread::started,
            m_batchWorker, &BatchWorker::process);

    // Signals → UI
    connect(m_batchWorker, &BatchWorker::log,
            ui->tbPreview, &QPlainTextEdit::appendPlainText);
    connect(m_batchWorker, &BatchWorker::progress,
            this, &MainWindow::onBatchProgress);
    connect(m_batchWorker, &BatchWorker::error,
            this, &MainWindow::onBatchError);
    connect(m_batchWorker, &BatchWorker::finished,
            this, &MainWindow::onBatchFinished);

    // Cleanup when worker finishes
    connect(m_batchWorker, &BatchWorker::finished,
            m_batchThread, &QThread::quit);
    connect(m_batchThread, &QThread::finished,
            m_batchWorker, &QObject::deleteLater);
    connect(m_batchThread, &QThread::finished,
            this, &MainWindow::onBatchThreadFinished);

    // --- Show Cancel button while running (reuse existing button) ---
    m_cancelPdfButton->setEnabled(true);
    m_cancelPdfButton->show();

    m_batchThread->start();
}


void MainWindow::on_btnCopy_clicked() const {
    if (ui->tbDestination->document()->isEmpty()) {
        ui->statusBar->showMessage("Destination content empty.");
        return;
    }

    try {
        QGuiApplication::clipboard()->setText(
            ui->tbDestination->document()->toPlainText());
    } catch (...) {
        ui->statusBar->showMessage("Clipboard error.");
        return;
    }
    ui->statusBar->showMessage("Destination contents copied to clipboard");
}

void MainWindow::on_btnOpenFile_clicked() {
    const QString file_name = QFileDialog::getOpenFileName(
        this,
        tr("Open File"),
        ".",
        tr("Text Files (*.txt);;"
            "Subtitle Files (*.srt *.vtt *.ass *.ttml2 *.xml);;"
            "XML Files (*.xml *.ttml2);;"
            "PDF Files (*.pdf);;"
            "All Files (*.*)")
    );

    if (file_name.isEmpty())
        return;

    // ----- If it's a PDF → use PdfExtractWorker -----
    if (isPdf(file_name)) {
        // Show in the status bar
        ui->statusBar->showMessage(tr("Opening PDF: %1").arg(file_name));

        // Start PDF extraction in worker thread
        startPdfExtraction(file_name);
        return;
    }

    // ----- Otherwise: open as text -----
    QFile file(file_name);

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ui->statusBar->showMessage(tr("Error opening file: %1").arg(file.errorString()));
        return;
    }

    QTextStream in(&file);
    const QString file_content = in.readAll();
    file.close();

    ui->tbSource->document()->setPlainText(file_content);
    ui->tbSource->contentFilename = file_name;

    ui->statusBar->showMessage(QStringLiteral("File: %1").arg(file_name));

    const int text_code = ZhoCheck(file_content.toStdString());
    update_tbSource_info(text_code);
}

bool MainWindow::isPdf(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QByteArray head = f.read(64); // enough for all real PDFs
    const qsizetype index = head.indexOf("%PDF-");
    return index >= 0;
}

// void MainWindow::on_btnReflow_clicked() const {
//     const QString src = ui->tbSource->toPlainText();
//     if (src.trimmed().isEmpty()) {
//         ui->statusBar->showMessage(tr("Source text is empty. Nothing to reflow."));
//         return;
//     }
//
//     // Convert to UTF-8 std::string
//     const QByteArray utf8 = src.toUtf8();
//     const std::string input(utf8.constData(), static_cast<std::size_t>(utf8.size()));
//
//     // Use same addPdfPageHeader flag as extraction, or tie to a checkbox if you like
//     const bool addPdfPageHeader = ui->actionAddPageHeader->isChecked(); // or a setting
//     const bool compact = ui->actionCompactPdfText->isChecked(); // false = blank line between paragraphs
//
//     const std::string reflowed = pdfium::ReflowCjkParagraphs(input, addPdfPageHeader, compact);
//
//     // Back to QString
//     const QString out = QString::fromUtf8(reflowed.c_str(),
//                                           static_cast<int>(reflowed.size()));
//
//     ui->tbSource->setPlainText(out);
//     ui->statusBar->showMessage(tr("✅ Text reflow complete."));
// }
void MainWindow::on_btnReflow_clicked() const {
    auto *edit = ui->tbSource;
    QTextCursor cursor = edit->textCursor();
    const bool hasSelection = cursor.hasSelection();

    QString src;
    if (hasSelection) {
        // Only reflow the selected range
        src = cursor.selection().toPlainText();
    } else {
        // Reflow the whole document
        src = edit->toPlainText();
    }

    if (src.trimmed().isEmpty()) {
        ui->statusBar->showMessage(tr("Source text is empty. Nothing to reflow."));
        return;
    }

    // Convert to UTF-8 std::string
    const QByteArray utf8 = src.toUtf8();
    const std::string input(utf8.constData(),
                            static_cast<std::size_t>(utf8.size()));

    const bool addPdfPageHeader = ui->actionAddPageHeader->isChecked();
    const bool compact = ui->actionCompactPdfText->isChecked();

    const std::string reflowed =
            pdfium::ReflowCjkParagraphs(input, addPdfPageHeader, compact);

    // Back to QString
    const QString out = QString::fromUtf8(reflowed.c_str(),
                                          static_cast<int>(reflowed.size()));

    // ✅ Replace text via QTextCursor so undo history is preserved
    if (auto *doc = edit->document(); doc->isUndoRedoEnabled()) {
        if (hasSelection) {
            // Reflow only selection → one undo step
            cursor.beginEditBlock();
            cursor.insertText(out); // replaces the selection
            cursor.endEditBlock();
            edit->setTextCursor(cursor);
        } else {
            // No selection → reflow entire document → one undo step
            QTextCursor docCursor(doc);
            docCursor.beginEditBlock();
            docCursor.select(QTextCursor::Document); // select all existing text
            docCursor.insertText(out); // replace with reflowed text
            docCursor.endEditBlock();
        }
    } else {
        // Fallback (if you ever disable undo somewhere else)
        if (hasSelection) {
            cursor.insertText(out);
            edit->setTextCursor(cursor);
        } else {
            edit->setPlainText(out);
        }
    }

    ui->statusBar->showMessage(tr("✅ Text reflow complete."));
}

void MainWindow::on_btnSaveAs_clicked() {
    // Determine which text box to save
    QString targetName = ui->cbSaveTarget->currentText();
    QString content;

    if (targetName == "Source") {
        content = ui->tbSource->toPlainText();
    } else {
        content = ui->tbDestination->toPlainText();
    }
    // Suggested filename like "./Source.txt"
    const QString suggested = QString("./%1.txt").arg(targetName);
    // Dialog
    const QString filename =
            QFileDialog::getSaveFileName(
                this,
                tr("Save Text File"),
                suggested,
                tr("Text File (*.txt);;All Files (*.*)")
            );

    if (filename.isEmpty())
        return;

    // Write file
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        ui->statusBar->showMessage("❌ Cannot open file for writing.");
        return;
    }

    QTextStream out(&file);
    out << content;
    file.close();

    ui->statusBar->showMessage(
        QStringLiteral("💾 File saved (%1): %2")
        .arg(targetName, filename)
    );
}

void MainWindow::refreshFromSource() const {
    if (ui->tbSource->toPlainText().isEmpty())
        return;

    const int text_code =
            openccFmmsegHelper.zhoCheck(ui->tbSource->toPlainText().toStdString());
    update_tbSource_info(text_code);
}

void MainWindow::on_tbSource_textChanged() const {
    // const QLocale locale;
    ui->lblCharCount->setText(
        QStringLiteral("[ %L1 chars ]")
        // .arg(locale.toString(ui->tbSource->document()->toPlainText().length())));
        .arg(ui->tbSource->document()->toPlainText().length()));
}

void MainWindow::on_btnAdd_clicked() {
    QFileDialog file_dialog(this);
    file_dialog.setFileMode(QFileDialog::ExistingFiles);

    if (const QStringList files =
            QFileDialog::getOpenFileNames(this,
                                          "Open Files",
                                          "",
                                          "Text Files (*.txt);;"
                                          "Subtitle Files (*.srt *.vtt *.ass *.ttml2 *.xml);;"
                                          "Office Files (*.docx *.xlsx *.pptx *.odt *.ods *.odp *.epub);;"
                                          "PDF Files (*.pdf);;"
                                          "All Files (*.*)"); !files.isEmpty()) {
        displayFileList(files);
        ui->statusBar->showMessage("File(s) added.");
    }
}

void MainWindow::displayFileList(const QStringList &files) const {
    // Find insertion point: first PDF index
    int insertPdfAt = ui->listSource->count();

    // Move backwards to find last non-PDF
    for (int i = ui->listSource->count() - 1; i >= 0; --i) {
        if (QString text = ui->listSource->item(i)->text(); !text.endsWith(".pdf", Qt::CaseInsensitive)) {
            insertPdfAt = i + 1;
            break;
        }
    }

    for (const QString &file: files) {
        if (filePathExists(file))
            continue;

        if (isPdf(file)) {
            // Insert PDF at the predefined position
            ui->listSource->insertItem(insertPdfAt, file);

            // Move the insertion point down (next PDF should follow)
            insertPdfAt++;
        } else {
            // Normal file, append at top section
            ui->listSource->addItem(file);
        }
    }
}

bool MainWindow::filePathExists(const QString &file_path) const {
    // Check if the file path is already in the list box
    for (int index = 0; index < ui->listSource->count(); ++index) {
        if (const QListWidgetItem *item = ui->listSource->item(index); item && item->text() == file_path) {
            return true;
        }
    }
    return false;
}

void MainWindow::on_btnRemove_clicked() const {
    if (QList<QListWidgetItem *> selected_items = ui->listSource->selectedItems(); !selected_items.isEmpty()) {
        for (qsizetype i = selected_items.size() - 1; i >= 0; --i) {
            const QListWidgetItem *selected_item = selected_items[i];
            const int row = ui->listSource->row(selected_item);
            ui->listSource->takeItem(row);
            delete selected_item;
        }
        ui->statusBar->showMessage("File(s) removed.");
    }
}

void MainWindow::on_btnListClear_clicked() const {
    ui->listSource->clear();
    ui->statusBar->showMessage("All entries cleared.");
}

void MainWindow::on_btnPreview_clicked() const {
    if (QList<QListWidgetItem *> selected_items = ui->listSource->selectedItems(); !selected_items.isEmpty()) {
        const QListWidgetItem *selected_item = selected_items[0];
        const QString file_path = selected_item->text();

        QFile file(file_path);
        if (const QFileInfo file_info(file_path); isAllowedTextLike(file_info.suffix().toLower())
                                                  && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            const QString contents = in.readAll();
            file.flush();
            file.close();
            ui->tbPreview->setPlainText(contents);
            ui->statusBar->showMessage("Preview: " + file_path);
        } else {
            ui->tbPreview->clear();
            ui->tbPreview->setPlainText(file_info.fileName() + ": ❌ Not a valid text file.");
            ui->statusBar->showMessage(file_path + ": Not a valid text file.");
        }
    }
}

void MainWindow::on_btnOutDir_clicked() {
    QFileDialog file_dialog(this);
    file_dialog.setFileMode(QFileDialog::Directory);

    if (const QString directory = QFileDialog::getExistingDirectory(this, ""); !directory.isEmpty()) {
        ui->lineEditDir->setText(directory);
        ui->statusBar->showMessage("Output directory set: " + directory);
    }
}

void MainWindow::on_btnPreviewClear_clicked() const {
    ui->tbPreview->clear();
    ui->statusBar->showMessage("Preview contents cleared");
}

void MainWindow::on_btnClearTbSource_clicked() const {
    ui->tbSource->clear();
    ui->lblSourceCode->setText("");
    ui->lblFileName->setText("");
    ui->statusBar->showMessage("Source contents cleared");
}

void MainWindow::on_btnClearTbDestination_clicked() const {
    ui->tbDestination->clear();
    ui->lblDestinationCode->setText("");
    ui->statusBar->showMessage("Destination contents cleared");
}

void MainWindow::on_cbManual_activated() const {
    ui->rbManual->setChecked(true);
}
