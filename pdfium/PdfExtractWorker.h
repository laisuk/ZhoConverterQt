// PdfExtractWorker.h
#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <atomic>
#include <utility>

#include "PdfiumHelper.hpp"

class PdfExtractWorker final : public QObject {
    Q_OBJECT

public:
    explicit PdfExtractWorker(QString filePath,
                              const bool addPdfPageHeader = true,
                              QObject *parent = nullptr)
        : QObject(parent)
          , m_filePath(std::move(filePath))
          , m_addPdfPageHeader(addPdfPageHeader)
          , m_cancelFlag(std::make_shared<std::atomic<bool> >(false)) {
    }

public slots:
    // Entry point for the worker thread
    void process();

    // Called from GUI thread to request cancellation
    void requestCancel() const {
        m_cancelFlag->store(true, std::memory_order_relaxed);
    }

signals:
    // percent: 0–100
    // bar: emoji progress bar (🟩🟩⬜⬜…)
    // pageIndex: 0-based
    // pageCount: total pages
    void progressChanged(int percent,
                         const QString &bar,
                         int pageIndex,
                         int pageCount);

    // Emitted when extraction finished normally
    void finished(const QString &text);

    // Emitted when user cancelled with partial extracted text
    void cancelled(const QString &partialText);

    // Emitted on error
    void errorOccurred(const QString &message);

public:
    // ✅ 新增：同步 helper，給 BatchWorker 用
    static QString extractPdfTextBlocking(
        const QString &filePath,
        bool addPdfPageHeader,
        bool autoReflowPdf,
        bool compactPdf,
        const std::function<bool()> &isCancelled);

private:
    QString m_filePath;
    bool m_addPdfPageHeader;
    std::shared_ptr<std::atomic<bool> > m_cancelFlag;
};
