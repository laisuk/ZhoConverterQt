// PdfExtractWorker.cpp

#include "PdfExtractWorker.h"
#include "PdfiumHelper.hpp"

#include <QString>
#include <QDebug>
#include <atomic>

// 你原本的 process() 保留不變
void PdfExtractWorker::process() {
    try {
        const pdfium::ProgressCallback progressCb =
                [this](const int pageIndex,
                       const int pageCount,
                       const int percent,
                       const std::string &barUtf8) {
            if (m_cancelFlag->load(std::memory_order_relaxed))
                return;

            const QString bar = QString::fromUtf8(barUtf8.c_str(),
                                                  static_cast<int>(barUtf8.size()));
            emit progressChanged(percent, bar, pageIndex, pageCount);
        };

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

// ✅ 新增：給 BatchWorker 用的同步 helper
QString PdfExtractWorker::extractPdfTextBlocking(
    const QString &filePath,
    const bool addPdfPageHeader,
    const bool autoReflowPdf,
    const bool compactPdf,
    const std::function<bool()> &isCancelled) {
    // 本地 cancel flag，傳給 pdfium::ExtractText
    std::atomic<bool> cancelFlag(false);

    const QByteArray pathUtf8 = filePath.toUtf8();

    const pdfium::ProgressCallback progressCb =
            [&isCancelled, &cancelFlag](const int pageIndex,
                                        const int pageCount,
                                        const int percent,
                                        const std::string &barUtf8) {
        Q_UNUSED(pageIndex);
        Q_UNUSED(pageCount);
        Q_UNUSED(percent);
        Q_UNUSED(barUtf8);

        // 外面要求 cancel → 設 flag，讓 pdfium 停
        if (isCancelled && isCancelled()) {
            cancelFlag.store(true, std::memory_order_relaxed);
        }
    };

    try {
        std::string textUtf8 =
                pdfium::ExtractText(
                    pathUtf8.constData(),
                    addPdfPageHeader,
                    progressCb,
                    &cancelFlag
                );

        // 被取消就直接回空
        if ((isCancelled && isCancelled()) ||
            cancelFlag.load(std::memory_order_relaxed)) {
            return {};
        }

        if (textUtf8.empty()) {
            return {};
        }

        // 🔁 CJK Reflow + Compact（由設定控制）
        if (autoReflowPdf) {
            textUtf8 = pdfium::ReflowCjkParagraphs(
                textUtf8,
                addPdfPageHeader,
                compactPdf
            );
        }

        return QString::fromUtf8(textUtf8.c_str(),
                                 static_cast<int>(textUtf8.size()));
    } catch (const std::exception &ex) {
        qWarning() << "PDF extract error:" << ex.what();
        return {};
    } catch (...) {
        qWarning() << "Unknown error during PDF extract.";
        return {};
    }
}
