// PdfExtractWorker.cpp
#include "PdfExtractWorker.h"

#include <QString>
#include <QDebug>

void PdfExtractWorker::process() {
    try {
        // Build the progress callback for pdfium::ExtractText
        const pdfium::ProgressCallback progressCb =
                [this](const int pageIndex,
                       const int pageCount,
                       const int percent,
                       const std::string &barUtf8) {
            // If already cancelled, we don't emit anything
            if (m_cancelFlag->load(std::memory_order_relaxed))
                return;

            const QString bar = QString::fromUtf8(barUtf8.c_str(),
                                                  static_cast<int>(barUtf8.size()));
            emit progressChanged(percent, bar, pageIndex, pageCount);
        };

        // Call the synchronous backend (this runs in the worker thread)
        const std::string textUtf8 =
                pdfium::ExtractText(
                    m_filePath.toUtf8().constData(),
                    m_addPdfPageHeader,
                    progressCb,
                    m_cancelFlag.get()
                );

        if (m_cancelFlag->load(std::memory_order_relaxed)) {
            const QString partial =
                    QString::fromUtf8(textUtf8.c_str(),
                                      static_cast<int>(textUtf8.size()));
            emit cancelled(partial);
            return;
        }

        const QString text =
                QString::fromUtf8(textUtf8.c_str(),
                                  static_cast<int>(textUtf8.size()));
        emit finished(text);
    } catch (const std::exception &ex) {
        emit errorOccurred(QString::fromUtf8(ex.what()));
    } catch (...) {
        emit errorOccurred(tr("Unknown error during PDF extraction."));
    }
}
