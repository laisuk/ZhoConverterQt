#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_mainwindow.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindowClass; };
QT_END_NAMESPACE

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:

    [[maybe_unused]] void on_btnExit_clicked();

    [[maybe_unused]] static void on_actionExit_triggered();

    [[maybe_unused]] void on_actionAbout_triggered();

	void on_tabWidget_currentChanged(int index) const;

	void on_rbStd_clicked() const;

	void on_rbHK_clicked() const;

	void on_rbZHTW_clicked() const;

	void on_cbTWCN_stateChanged(int state) const;

	void on_btnPaste_clicked() const;

	void on_btnProcess_clicked() const;

	void on_btnCopy_clicked() const;

	void on_btnOpenFile_clicked();

	void on_btnSaveAs_clicked();

	void on_btnRefresh_clicked() const;

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

private:
    Ui::MainWindowClass *ui;

	void displayFileList(const QStringList& files) const;
	[[nodiscard]] bool filePathExists(const QString& file_path) const;
	void update_tbSource_info(int text_code) const;
	[[nodiscard]] QString getCurrentConfig() const;

};
