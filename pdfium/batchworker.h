#pragma once

#include <QObject>
#include <QStringList>
#include <QDir>
#include <QAtomicInteger>

class OpenccFmmsegHelper; // forward-declare your helper

class BatchWorker : public QObject
{
    Q_OBJECT

public:
    BatchWorker(const QStringList &files,
                const QString &outDir,
                OpenccFmmsegHelper *converter,
                const QString &config,
                bool isPunctuation,
                bool convertFilename,
                bool addPdfPageHeader,
               bool autoReflowPdf,
               bool compactPdf,
                QObject *parent = nullptr);

public slots:
    void process();
    void requestCancel();

    signals:
        void log(const QString &line);
    void progress(int current, int total); // (idx, total)
    void finished(bool cancelled);
    void error(const QString &msg);

private:
    void processOneFile(int idx, int total, const QString &path);
    void processPdf(int idx, int total, const QString &path, const QString &baseName);

QStringList m_files;
    QString     m_outDir;
    OpenccFmmsegHelper *m_converter;
    QString     m_config;
    bool        m_isPunctuation;
    bool        m_convertFilename;
    bool        m_addPdfPageHeader;
    bool        m_autoReflowPdf;
    bool        m_compactPdf;

    QAtomicInteger<bool> m_cancelRequested;
};
