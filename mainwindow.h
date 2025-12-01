#pragma once

#include "ui_mainwindow.h"
#include "OpenccFmmsegHelper.hpp"
#include "PdfExtractWorker.h"

QT_BEGIN_NAMESPACE

namespace Ui {
    class MainWindowClass;
};

QT_END_NAMESPACE

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    ~MainWindow() override;

private slots:
    void on_btnExit_clicked();

    static void on_actionExit_triggered();

    void on_actionAbout_triggered();

    void on_tabWidget_currentChanged(int index) const;

    void on_rbStd_clicked() const;

    void on_rbHK_clicked() const;

    void on_rbZHTW_clicked() const;

    void on_cbTWCN_stateChanged(int state) const;

    void on_btnPaste_clicked() const;

    void on_btnProcess_clicked();

    void on_btnCopy_clicked() const;

    void on_btnOpenFile_clicked();

    static bool isPdf(const QString &path);

    void on_btnReflow_clicked() const;

    void on_btnSaveAs_clicked();

    void on_tbSource_textChanged() const;

    void on_btnAdd_clicked();

    void on_btnRemove_clicked() const;

    void on_btnListClear_clicked() const;

    void on_btnPreview_clicked() const;

    void on_btnOutDir_clicked();

    void on_btnPreviewClear_clicked() const;

    void on_btnClearTbSource_clicked() const;

    void on_btnClearTbDestination_clicked() const;

    void on_cbManual_activated() const;

    void refreshFromSource() const;

    void onPdfExtractionFinished(const QString &text);

    void onPdfExtractionCancelled(const QString &partialText);

    void onPdfExtractionError(const QString &message);

    void cleanupPdfThread();

    void onCancelPdfClicked() const;

private:
    Ui::MainWindowClass *ui;

    void displayFileList(const QStringList &files) const;

    [[nodiscard]] bool filePathExists(const QString &file_path) const;

    void update_tbSource_info(int text_code) const;

    void main_process(const QString &config, bool is_punctuation) const;

    void batch_process(const QString &config, bool is_punctuation);

    [[nodiscard]] QString getCurrentConfig() const;

    // void *openccInstance = nullptr;
    OpenccFmmsegHelper openccFmmsegHelper;

    QPushButton *m_cancelPdfButton = nullptr; // button shown in status bar
    QThread *m_pdfThread = nullptr;
    PdfExtractWorker *m_pdfWorker = nullptr;
    QString m_currentPdfFilePath; // <--- add this

    void startPdfExtraction(const QString &filePath);
};
