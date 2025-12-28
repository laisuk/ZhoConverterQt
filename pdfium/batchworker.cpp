// BatchWorker.cpp
#include "batchworker.h"
#include "PdfExtractWorker.h"

#include <QFileInfo>
#include <QFile>
#include <QPlainTextEdit>
#include <QTextStream>

#include "openccfmmseghelper.hpp"   // adjust include name
#include "filetype_utils.h"
#include "OfficeConverter.hpp"
// #include "OfficeConverterMinizip.hpp"

class QPlainTextEdit;

BatchWorker::BatchWorker(const QStringList &files,
                         const QString &outDir,
                         OpenccFmmsegHelper *converter,
                         const opencc_config_t &config,
                         const bool isPunctuation,
                         const bool convertFilename,
                         const bool addPdfPageHeader,
                         const bool autoReflowPdf,
                         const bool compactPdf,
                         QObject *parent)
    : QObject(parent),
      m_files(files),
      m_outDir(outDir),
      m_converter(converter),
      m_config(config),
      m_isPunctuation(isPunctuation),
      m_convertFilename(convertFilename),
      m_addPdfPageHeader(addPdfPageHeader),
      m_autoReflowPdf(autoReflowPdf),
      m_compactPdf(compactPdf),
      m_cancelRequested(false) {
}

void BatchWorker::requestCancel() {
    m_cancelRequested.storeRelaxed(true);
}

void BatchWorker::process() {
    const qsizetype total = m_files.size();
    if (total == 0) {
        emit finished(false);
        return;
    }

    if (const QDir dir(m_outDir); !dir.exists()) {
        auto _ = dir.mkpath(".");
    }

    for (qsizetype i = 0; i < m_files.size(); ++i) {
        const qsizetype idx = i + 1;

        if (m_cancelRequested.loadRelaxed()) {
            emit log(QStringLiteral("Batch cancelled."));
            emit finished(true);
            return;
        }

        const QString path = m_files.at(i);
        if (QFileInfo fi(path); !fi.exists()) {
            emit log(QString("%1: %2 -> ❌ File not found.")
                .arg(idx)
                .arg(path));
            emit progress(static_cast<int>(idx),
                          static_cast<int>(total));

            continue;
        }

        try {
            processOneFile(static_cast<int>(idx), static_cast<int>(total), path);
        } catch (const std::exception &e) {
            emit error(QString("%1: %2 -> Error: %3")
                .arg(idx)
                .arg(path, QString::fromUtf8(e.what())));
        } catch (...) {
            emit error(QString("%1: %2 -> Unknown error.")
                .arg(idx)
                .arg(path));
        }

        emit emit progress(static_cast<int>(idx),
                           static_cast<int>(total));
    }

    emit finished(false);
}

void BatchWorker::processOneFile(const int idx, const int total, const QString &path) {
    const QFileInfo fi(path);
    const QString extLower = fi.suffix().toLower();
    const bool noExt = extLower.isEmpty();
    QString baseName = fi.baseName();

    // Optional: convert filename stem (no punctuation for names unless you want it)
    if (m_convertFilename && m_converter) {
        const std::string convertedName =
                m_converter->convert_cfg(baseName.toStdString(),
                                         m_config,
                                         /*punctuation=*/false);
        baseName = QString::fromStdString(convertedName);
    }

    // --- PDF route (stub, integrate later with your PDF helper if desired) ---
    if (extLower == "pdf") {
        processPdf(idx, total, path, baseName);
        return;
    }

    // --- Office route ---
    if (isOfficeExt(extLower)) {
        // your existing helper
        const QString outPath =
                makeOutputPath(m_outDir, baseName, m_config, extLower);

        if (path == outPath) {
            emit log(QString("%1: %2 -> ❌ Skip: Output Path = Source Path.")
                .arg(idx)
                .arg(outPath));
            return;
        }

        // We already know QFileInfo exists
        auto [ok, msg] = OfficeConverter::Convert(
            path.toStdString(),
            outPath.toStdString(),
            extLower.toStdString(),
            *m_converter,
            m_config,
            m_isPunctuation,
            /*keepFont=*/true
        );

        emit log(QString("%1: %2 -> %3")
            .arg(idx)
            .arg(outPath, QString::fromStdString(msg)));
        return;
    }

    // --- Text-like route (includes NO extension) ---
    if (!isAllowedTextLike(extLower)) {
        emit log(QString("%1: %2 -> ❌ Skip: Unsupported file type.")
            .arg(idx)
            .arg(path));
        return;
    }

    const QString outPath =
            makeOutputPath(m_outDir, baseName, m_config, extLower);

    if (path == outPath) {
        emit log(QString("%1: %2 -> ❌ Skip: Output Path = Source Path.")
            .arg(idx)
            .arg(outPath));
        return;
    }

    QFile inFile(path);
    if (!inFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit log(QString("%1: %2 -> ❌ Error opening for read.")
            .arg(idx)
            .arg(path));
        return;
    }

    const QString inputText = QTextStream(&inFile).readAll();
    inFile.close();

    const std::string converted =
            m_converter->convert_cfg(inputText.toStdString(),
                                     m_config,
                                     m_isPunctuation);

    QFile outFile(outPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit log(QString("%1: %2 -> ❌ Error opening for write: %3")
            .arg(idx)
            .arg(outPath, outFile.errorString()));
        return;
    } {
        QTextStream ts(&outFile);
        ts.setEncoding(QStringConverter::Utf8); // Qt 6
        ts << QString::fromStdString(converted);
    }
    outFile.flush();
    outFile.close();

    if (noExt) {
        emit log(QString("%1: %2 -> ✅ Done (treated as text: no extension).")
            .arg(idx)
            .arg(outPath));
    } else {
        emit log(QString("%1: %2 -> ✅ Done.")
            .arg(idx)
            .arg(outPath));
    }
}

void BatchWorker::processPdf(const int idx, const int total,
                             const QString &path,
                             const QString &baseName) {
    Q_UNUSED(total);

    // PDF 一律輸出為 .txt
    const QString outPath =
            QDir(m_outDir).filePath(baseName + "_" + OpenccFmmsegHelper::config_id_to_name(m_config).data() + ".txt");

    emit log(QString("%1: %2 -> Extracting PDF text...")
        .arg(idx)
        .arg(path));

    // 同步抽取 PDF 文字（含 PageHeader / Reflow / Compact）
    const QString rawText = PdfExtractWorker::extractPdfTextBlocking(
        path,
        m_addPdfPageHeader,
        m_autoReflowPdf,
        m_compactPdf,
        [this]() { return m_cancelRequested.loadRelaxed(); });

    // 被取消或空內容
    if (m_cancelRequested.loadRelaxed()) {
        emit log(QString("%1: %2 -> ❌ Cancelled during PDF extraction.")
            .arg(idx)
            .arg(path));
        return;
    }

    if (rawText.isEmpty()) {
        emit log(QString("%1: %2 -> ❌ Empty or non-text PDF.")
            .arg(idx)
            .arg(path));
        return;
    }

    // OpenCC 轉換
    const std::string converted =
            m_converter->convert_cfg(
                rawText.toStdString(),
                m_config,
                m_isPunctuation);

    QFile outFile(outPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit log(QString("%1: %2 -> ❌ Error opening for write: %3")
            .arg(idx)
            .arg(outPath, outFile.errorString()));
        return;
    } {
        QTextStream ts(&outFile);
        ts.setEncoding(QStringConverter::Utf8);
        ts << QString::fromStdString(converted);
    }
    outFile.close();

    emit log(QString("%1: %2 -> ✅ Done.")
        .arg(idx)
        .arg(outPath));
}
