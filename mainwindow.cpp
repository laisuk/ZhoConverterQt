#include "mainwindow.h"
#include "QClipboard"
#include "QFileDialog"
#include "QSaveFile"
#include "QMessageBox"
#include <QFileInfo>
#include <QMenu>
#include <QMouseEvent>
#include <QSet>
#include <QStringDecoder>
#include <QThread>
#include <QTextDocumentFragment>
#include <string>

#include "EncodingDetector.h"

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <iconv.h>
#endif
// #include "opencc_fmmseg_capi.h"
#include "zhoutilities.h"
#include "draglistwidget.h"
#include "filetype_utils.h"
#include "OfficeConverter.hpp"
// #include "OfficeConverterMinizip.hpp"
#include "AboutDialog.h"
#include "ReflowHelper.hpp"


namespace {
    QString stripBom(QString text) {
        if (!text.isEmpty() && text.front() == QChar::ByteOrderMark)
            text.remove(0, 1);
        return text;
    }

    QString decodeUtf(const QByteArray &bytes,
                      const QStringDecoder::Encoding encoding,
                      bool *ok) {
        QStringDecoder decoder(encoding);
        QString text = decoder.decode(bytes);

        if (decoder.hasError()) {
            if (ok)
                *ok = false;
            return {};
        }

        if (ok)
            *ok = true;

        return stripBom(std::move(text));
    }

#ifdef Q_OS_WIN

    QString decodeWindowsCodePage(const QByteArray &bytes,
                                  const UINT codePage,
                                  bool *ok) {
        if (bytes.isEmpty()) {
            if (ok)
                *ok = true;
            return {};
        }

        const int required = MultiByteToWideChar(
            codePage,
            MB_ERR_INVALID_CHARS,
            bytes.constData(),
            static_cast<int>(bytes.size()),
            nullptr,
            0);

        if (required <= 0) {
            if (ok)
                *ok = false;
            return {};
        }

        QString text(required, Qt::Uninitialized);

        const int written = MultiByteToWideChar(
            codePage,
            MB_ERR_INVALID_CHARS,
            bytes.constData(),
            static_cast<int>(bytes.size()),
            reinterpret_cast<wchar_t *>(text.data()),
            required);

        if (written <= 0) {
            if (ok)
                *ok = false;
            return {};
        }

        text.resize(written);

        if (ok)
            *ok = true;

        return stripBom(std::move(text));
    }

#else

    QString decodeIconv(const QByteArray &bytes,
                        const char *fromEncoding,
                        bool *ok) {
        iconv_t cd = iconv_open("UTF-8", fromEncoding);
        if (cd == reinterpret_cast<iconv_t>(-1)) {
            if (ok)
                *ok = false;
            return {};
        }

        const char *inputConst = bytes.constData();
        std::size_t inputLeft = static_cast<std::size_t>(bytes.size());

        QByteArray output;
        output.resize(qMax(bytes.size() * 4, 64));

        char *outputPtr = output.data();
        std::size_t outputLeft = static_cast<std::size_t>(output.size());

        while (inputLeft > 0) {
            char *inputPtr = const_cast<char *>(inputConst);

            const std::size_t result =
                    iconv(cd, &inputPtr, &inputLeft, &outputPtr, &outputLeft);

            inputConst = inputPtr;

            if (result != static_cast<std::size_t>(-1))
                continue;

            if (errno == E2BIG) {
                const qsizetype used =
                        static_cast<qsizetype>(outputPtr - output.data());

                output.resize(output.size() * 2);
                outputPtr = output.data() + used;
                outputLeft =
                        static_cast<std::size_t>(output.size() - used);
                continue;
            }

            iconv_close(cd);

            if (ok)
                *ok = false;
            return {};
        }

        iconv_close(cd);

        output.resize(
            static_cast<qsizetype>(outputPtr - output.data()));

        QStringDecoder decoder(QStringDecoder::Utf8);
        QString text = decoder.decode(output);

        if (decoder.hasError()) {
            if (ok)
                *ok = false;
            return {};
        }

        if (ok)
            *ok = true;

        return stripBom(std::move(text));
    }

#endif

    QString decodeTextBytes(const QByteArray &bytes,
                            const QString &encoding,
                            bool *ok) {
        if (encoding.compare(QStringLiteral("UTF-8"),
                             Qt::CaseInsensitive) == 0)
            return decodeUtf(bytes, QStringDecoder::Utf8, ok);

        if (encoding.compare(QStringLiteral("UTF-16LE"),
                             Qt::CaseInsensitive) == 0)
            return decodeUtf(bytes, QStringDecoder::Utf16LE, ok);

        if (encoding.compare(QStringLiteral("UTF-16BE"),
                             Qt::CaseInsensitive) == 0)
            return decodeUtf(bytes, QStringDecoder::Utf16BE, ok);

#ifdef Q_OS_WIN
        if (encoding.compare(QStringLiteral("GB18030"),
                             Qt::CaseInsensitive) == 0)
            return decodeWindowsCodePage(bytes, 54936, ok);

        if (encoding.compare(QStringLiteral("Big5"),
                             Qt::CaseInsensitive) == 0)
            return decodeWindowsCodePage(bytes, 950, ok);

        // Windows has no separate Big5-HKSCS code page exposed here.
        // CP950 is used as the native fallback without adding dependencies.
        if (encoding.compare(QStringLiteral("Big5-HKSCS"),
                             Qt::CaseInsensitive) == 0)
            return decodeWindowsCodePage(bytes, 950, ok);
#else
        if (encoding.compare(QStringLiteral("GB18030"),
                             Qt::CaseInsensitive) == 0)
            return decodeIconv(bytes, "GB18030", ok);

        if (encoding.compare(QStringLiteral("Big5"),
                             Qt::CaseInsensitive) == 0)
            return decodeIconv(bytes, "BIG5", ok);

        if (encoding.compare(QStringLiteral("Big5-HKSCS"),
                             Qt::CaseInsensitive) == 0)
            return decodeIconv(bytes, "BIG5-HKSCS", ok);
#endif

        if (ok)
            *ok = false;
        return {};
    }
}

namespace {
    QString codecNameForDetectedEncoding(const EncodingDetector::Encoding encoding) {
        using Encoding = EncodingDetector::Encoding;

        switch (encoding) {
            case Encoding::Ascii:
            case Encoding::Utf8:
            case Encoding::Utf8Bom:
                return QStringLiteral("UTF-8");

            case Encoding::Utf16LE:
            case Encoding::Utf16LEBom:
                return QStringLiteral("UTF-16LE");

            case Encoding::Utf16BE:
            case Encoding::Utf16BEBom:
                return QStringLiteral("UTF-16BE");

            case Encoding::Big5:
                return QStringLiteral("Big5");

            case Encoding::Gb18030:
                return QStringLiteral("GB18030");

            case Encoding::ShiftJis:
                return QStringLiteral("Shift-JIS");

            case Encoding::Unknown:
            default:
                return {};
        }
    }
} // namespace// namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindowClass()) {
    ui->setupUi(this);
    ui->tabWidget->setCurrentIndex(0);
    // openccInstance = opencc_new();
    // opencc_set_parallel(openccInstance, false);
    connect(ui->tbSource, &TextEditWidget::fileDropped, this,
            [this](const QString &path) {
                if (path.isEmpty()) {
                    m_currentTextEncoding = QStringLiteral("UTF-8");
                    refreshFromSource();
                    ui->statusBar->showMessage("Text contents dropped");
                    return;
                }

                if (isPdf(path)) {
                    ui->statusBar->showMessage(tr("Opening PDF: %1").arg(path));
                    startPdfExtraction(path);
                    return;
                }

                loadTextFile(path);
            });

    ui->lblFileName->installEventFilter(this);
    ui->lblFileName->setCursor(Qt::PointingHandCursor);
    ui->lblFileName->setToolTip(
        tr("Click to reload the current text file using a different encoding."));

    // --- Status-bar Cancel button ---
    m_cancelPdfButton = new QPushButton(tr("Cancel"), this);
    m_cancelPdfButton->setObjectName("btnCancelPdf");
    m_cancelPdfButton->setAutoDefault(false);
    m_cancelPdfButton->setFlat(true); // look like status-bar control
    m_cancelPdfButton->hide(); // hidden by default

    ui->statusBar->addPermanentWidget(m_cancelPdfButton);

    connect(m_cancelPdfButton, &QPushButton::clicked,
            this, &MainWindow::onCancelPdfClicked);
    m_openccVersion = opencc_version_string();
    ui->statusBar->showMessage(QString("Loaded: opencc-fmmseg version %1").arg(m_openccVersion));
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

// void MainWindow::on_actionAbout_triggered() {
//     QMessageBox::about(this, "About",
//                        "ZhoConverter version 1.0.0 (c) 2025 Laisuk Lai");
// }
void MainWindow::on_actionAbout_triggered() {
    AboutInfo info;

    // Prefer central metadata instead of hardcoding
    info.app_name = QCoreApplication::applicationName();
    info.version = QCoreApplication::applicationVersion();
    info.author = "Laisuk Lai";
    info.year = "2026";
    info.description =
            "ZhoConverterQt — OpenCC-based Chinese conversion tool "
            "with document and PDF processing support.";

    info.website_text = "GitHub";
    info.website_url = "https://github.com/laisuk/ZhoConverterQt";

    info.license_text = "MIT License";
    info.license_url = "https://opensource.org/licenses/MIT";

    QString buildType;
#ifdef NDEBUG
    buildType = "Release";
#else
    buildType = "Debug";
#endif

    QString platform;
#ifdef Q_OS_WIN
    platform = "Windows";
#elif defined(Q_OS_MAC)
    platform = "macOS";
#elif defined(Q_OS_LINUX)
    platform = "Linux";
#else
    platform = "Unknown";
#endif

    const QString qtVersion = QT_VERSION_STR;

    info.details = QString("Qt: %1\nBuild: %2\nPlatform: %3\nOpencc-Fmmseg: %4")
            .arg(qtVersion, buildType, platform, m_openccVersion);

    AboutDialog dlg(info, this, windowIcon());
    dlg.exec();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (watched == ui->lblFileName &&
        event->type() == QEvent::MouseButtonPress) {
        if (const auto *mouseEvent = dynamic_cast<QMouseEvent *>(event); mouseEvent->button() == Qt::LeftButton) {
            showEncodingMenu();
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::loadTextFile(const QString &filePath,
                              const QString &requestedEncoding,
                              const bool showErrorDialog,
                              const bool strictDecoding) {
    QString actualEncoding = requestedEncoding;

    const auto reportError =
            [this, showErrorDialog, &filePath](const QString &encoding,
                                               const QString &detail) {
        const QString message =
                tr("Failed to load %1 using %2:\n%3")
                .arg(filePath, encoding, detail);

        ui->statusBar->showMessage(message, 8000);

        if (showErrorDialog) {
            QMessageBox::critical(
                this,
                tr("Encoding Error"),
                message);
        }
    };

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        reportError(
            requestedEncoding,
            file.errorString());
        return false;
    }

    QByteArray bytes = file.readAll();

    if (file.error() != QFileDevice::NoError) {
        reportError(
            requestedEncoding,
            file.errorString());
        return false;
    }

    // ------------------------------------------------------------
    // Auto detection
    // ------------------------------------------------------------

    const bool autoDetect =
            requestedEncoding.compare(
                QStringLiteral("Auto"),
                Qt::CaseInsensitive) == 0;

    if (autoDetect) {
        const auto [encoding, bomSize] = EncodingDetector::detect(bytes);

        actualEncoding =
                codecNameForDetectedEncoding(encoding);

        if (actualEncoding.isEmpty()) {
            // Detector knows only that this is some legacy 8-bit
            // encoding. Until uchardet is added, preserve the old
            // fallback behaviour.
            actualEncoding = QStringLiteral("UTF-8");
        }

        // Remove BOM before decoding.
        if (bomSize > 0)
            bytes.remove(0, bomSize);
    }

    // ------------------------------------------------------------
    // Decode
    // ------------------------------------------------------------

    bool decoded = false;

    QString contents =
            decodeTextBytes(
                bytes,
                actualEncoding,
                &decoded);

    if (!decoded) {
        if (strictDecoding) {
            reportError(
                actualEncoding,
                tr("The file contains invalid byte sequences for this encoding, "
                    "or the encoding is unavailable on this platform."));
            return false;
        }

        // Initial Open/Drop must still load the file so the user can
        // click the filename and choose the correct encoding.
        contents = QString::fromUtf8(bytes);
    }

    // ------------------------------------------------------------
    // Update editor state
    // ------------------------------------------------------------

    ui->tbSource->document()->setPlainText(contents);
    ui->tbSource->contentFilename = filePath;

    m_currentTextEncoding = actualEncoding;
    m_textEncodingWasAutoDetected = autoDetect;

    const int textCode =
            openccFmmsegHelper.zhoCheck(
                contents.toStdString());

    update_tbSource_info(textCode);

    if (decoded) {
        if (requestedEncoding.compare(
                QStringLiteral("Auto"),
                Qt::CaseInsensitive) == 0) {
            ui->statusBar->showMessage(
                tr("Auto-detected %1: %2")
                .arg(actualEncoding, filePath));
        } else {
            ui->statusBar->showMessage(
                tr("Loaded as %1: %2")
                .arg(actualEncoding, filePath));
        }
    } else {
        ui->statusBar->showMessage(
            tr("Encoding could not be identified automatically. "
                "Loaded with UTF-8 replacement characters; "
                "click the filename to choose a supported encoding: %1")
            .arg(filePath));
    }

    return true;
}

void MainWindow::showEncodingMenu() {
    const QString filePath = ui->tbSource->contentFilename;
    if (filePath.isEmpty())
        return;

    if (!isEncodingSelectableFile(filePath)) {
        ui->statusBar->showMessage(
            tr("Encoding selection is only available for plain text files."));
        return;
    }

    struct EncodingChoice {
        const char *label;
        const char *codec;
    };

    static constexpr EncodingChoice encodings[] = {
        {.label = "Auto Detect", .codec = "Auto"},
        {.label = "UTF-8", .codec = "UTF-8"},
        {.label = "GB18030 / GBK", .codec = "GB18030"},
        {.label = "Big5 / CP950", .codec = "Big5"},
#ifdef Q_OS_WIN
        {.label = "Big5-HKSCS (CP950 fallback)", .codec = "Big5-HKSCS"},
#else
        {.label = "Big5-HKSCS", .codec = "Big5-HKSCS"},
#endif
        {.label = "UTF-16 LE", .codec = "UTF-16LE"},
        {.label = "UTF-16 BE", .codec = "UTF-16BE"},
    };

    QMenu menu(this);

    for (const auto &[label, codec]: encodings) {
        QAction *action = menu.addAction(QString::fromLatin1(label));
        action->setCheckable(true);

        const QString codecName = QString::fromLatin1(codec);
        action->setChecked(
            codecName.compare(m_currentTextEncoding, Qt::CaseInsensitive) == 0);
        action->setData(codecName);
    }

    const QPoint pos =
            ui->lblFileName->mapToGlobal(ui->lblFileName->rect().bottomLeft());

    if (const QAction *selected = menu.exec(pos))
        reloadCurrentTextFile(selected->data().toString());
}

void MainWindow::reloadCurrentTextFile(const QString &encoding) {
    const QString filePath = ui->tbSource->contentFilename;
    if (filePath.isEmpty())
        return;

    loadTextFile(filePath, encoding, true, true);
}

bool MainWindow::isEncodingSelectableFile(const QString &filePath) {
    if (isPdf(filePath))
        return false;

    const QString suffix = QFileInfo(filePath).suffix().toLower();

    static const QSet<QString> documentSuffixes = {
        QStringLiteral("docx"),
        QStringLiteral("xlsx"),
        QStringLiteral("pptx"),
        QStringLiteral("odt"),
        QStringLiteral("ods"),
        QStringLiteral("odp"),
        QStringLiteral("epub"),
    };

    return !documentSuffixes.contains(suffix);
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
                ui->statusBar->showMessage("Loading PDF  " + bar + "  " + QString::number(percent) + "%");
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
void MainWindow::onCancelPdfClicked() const {
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
        m_currentTextEncoding = QStringLiteral("UTF-8");

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
        m_currentTextEncoding = QStringLiteral("UTF-8");

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

opencc_config_t MainWindow::getCurrentConfigId() const {
    if (ui->rbManual->isChecked()) {
        return OpenccFmmsegHelper::config_name_to_id(
            ui->cbManual->currentText()
            .split(' ')
            .first()
            .toStdString()
        );
    }

    if (ui->rbS2t->isChecked()) {
        if (ui->rbHK->isChecked()) {
            return ui->cbTWCN->isChecked()
                       ? OPENCC_CONFIG_S2HKP
                       : OPENCC_CONFIG_S2HK;
        }

        if (ui->rbStd->isChecked()) {
            return OPENCC_CONFIG_S2T;
        }

        return ui->cbTWCN->isChecked()
                   ? OPENCC_CONFIG_S2TWP
                   : OPENCC_CONFIG_S2TW;
    }

    if (ui->rbT2s->isChecked()) {
        if (ui->rbHK->isChecked()) {
            return ui->cbTWCN->isChecked()
                       ? OPENCC_CONFIG_HK2SP
                       : OPENCC_CONFIG_HK2S;
        }

        if (ui->rbStd->isChecked()) {
            return OPENCC_CONFIG_T2S;
        }

        return ui->cbTWCN->isChecked()
                   ? OPENCC_CONFIG_TW2SP
                   : OPENCC_CONFIG_TW2S;
    }

    return OPENCC_CONFIG_S2TW;
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
    // ui->cbTWCN->setCheckState(Qt::Unchecked);
    ui->cbTWCN->setEnabled(false);
}

void MainWindow::on_rbHK_clicked() const {
    ui->cbTWCN->setEnabled(true);
    // ui->cbTWCN->setCheckState(Qt::Unchecked);
}

void MainWindow::on_rbZHTW_clicked() const {
    ui->cbTWCN->setEnabled(true);
    // ui->cbTWCN->setCheckState(Qt::Checked);
}

void MainWindow::on_cbTWCN_stateChanged(const int state) const {
    if (state && ui->rbStd->isChecked()) {
        ui->rbZHTW->setChecked(true);
    }
}

void MainWindow::on_btnPaste_clicked() {
    if (QGuiApplication::clipboard()->text().isEmpty() ||
        QGuiApplication::clipboard()->text().isNull()) {
        ui->statusBar->showMessage("Clipboard empty");
        return;
    }

    QString text;

    try {
        text = QGuiApplication::clipboard()->text();
        ui->tbSource->document()->setPlainText(text);
        ui->tbSource->contentFilename.clear();
        m_currentTextEncoding = QStringLiteral("UTF-8");
        ui->statusBar->showMessage("Clipboard contents pasted.");
    } catch (...) {
        ui->statusBar->showMessage("Clipboard error.");
        return;
    }
    const int text_code = openccFmmsegHelper.zhoCheck(text.toStdString());
    update_tbSource_info(text_code);
}

void MainWindow::on_btnProcess_clicked() {
    // const QString config = getCurrentConfig();
    const opencc_config_t config = getCurrentConfigId();
    // openccFmmsegHelper.setConfig(config.toStdString());
    openccFmmsegHelper.setConfigId(config);

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
void MainWindow::main_process(const opencc_config_t &config, const bool is_punctuation) const {
    const QTextCursor cursor = ui->tbSource->textCursor();
    const bool hasSelection = cursor.hasSelection();

    const QString input = hasSelection
                              ? cursor.selection().toPlainText()
                              : ui->tbSource->toPlainText();

    if (input.isEmpty()) {
        ui->statusBar->showMessage(
            hasSelection ? "Selected source content is empty" : "Source content is empty"
        );
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

    QElapsedTimer timer;
    timer.start();

    const auto output = openccFmmsegHelper.convert_cfg(
        inUtf8.constData(),
        config,
        is_punctuation
    );

    const qint64 elapsedMs = timer.elapsed();

    ui->tbDestination->document()->clear();

    const QString sourceKind = hasSelection ? "Selected text" : "Source text";

    if (!output.data()) {
        ui->statusBar->showMessage(
            QString("%1 conversion failed in %2 ms. (%3)")
            .arg(sourceKind)
            .arg(elapsedMs)
            .arg(opencc_config_id_to_name(config))
        );
        return;
    }

    ui->tbDestination->document()->setPlainText(QString::fromUtf8(output));

    ui->statusBar->showMessage(
        QString("%1 conversion completed in %2 ms. (%3)")
        .arg(sourceKind)
        .arg(elapsedMs)
        .arg(opencc_config_id_to_name(config))
    );
}


void MainWindow::batch_process(const opencc_config_t &config, const bool is_punctuation) {
    startBatchProcess(config, is_punctuation);
}


void MainWindow::startBatchProcess(const opencc_config_t &config,
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
        ui->actionAddPageHeader->isChecked(), // 是否加 === [Page x/y] ===
        ui->actionAutoReflow->isChecked(), // 自動重排
        ui->actionCompactPdfText->isChecked(), // 緊湊模式
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

    // ----- Otherwise: load through the single text-file loader -----
    loadTextFile(file_name);
}

bool MainWindow::isPdf(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QByteArray head = f.read(64); // enough for all real PDFs
    const qsizetype index = head.indexOf("%PDF-");
    return index >= 0;
}

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
    const QString targetName = ui->cbSaveTarget->currentText();
    const QString content =
            (targetName == "Source")
                ? ui->tbSource->toPlainText()
                : ui->tbDestination->toPlainText();

    // Suggested filename like "./Source.txt"
    const QString suggested = QString("./%1.txt").arg(targetName);

    const QString filename = QFileDialog::getSaveFileName(
        this,
        tr("Save Text File"),
        suggested,
        tr("Text File (*.txt);;All Files (*.*)")
    );

    if (filename.isEmpty())
        return;

    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        ui->statusBar->showMessage(tr("❌ Cannot open file for writing."));
        return;
    }

    QTextStream out(&file);
    out << content;

    if (!file.commit()) {
        ui->statusBar->showMessage(tr("❌ Failed to save file."));
        return;
    }

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

void MainWindow::on_btnClearTbSource_clicked() {
    ui->tbSource->clear();
    ui->tbSource->contentFilename.clear();
    m_currentTextEncoding = QStringLiteral("UTF-8");
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
