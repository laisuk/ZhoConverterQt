#pragma once

#include <QDialog>
#include <QRegularExpression>
#include <QTextEdit>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTimer;

class FindReplaceDialog final : public QDialog {
public:
    explicit FindReplaceDialog(QPlainTextEdit *editor, QWidget *parent = nullptr);
    ~FindReplaceDialog() override;

    void openFind(bool replaceMode = false);

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QPlainTextEdit *m_editor;
    QLineEdit *m_find;
    QLineEdit *m_replace;
    QLabel *m_counter;
    QLabel *m_message;
    QCheckBox *m_case;
    QCheckBox *m_words;
    QCheckBox *m_regex;
    QPushButton *m_prev;
    QPushButton *m_next;
    QPushButton *m_replaceOne;
    QPushButton *m_replaceAll;
    QWidget *m_replaceRow;
    QTimer *m_refreshTimer;
    bool m_replaceVisible = false;

    [[nodiscard]] QTextDocument::FindFlags flags(bool backward = false) const;
    [[nodiscard]] QRegularExpression expression() const;
    [[nodiscard]] QTextCursor search(const QTextCursor &from, bool backward = false) const;
    [[nodiscard]] bool validQuery() const;
    [[nodiscard]] bool isCurrentMatch() const;
    void navigate(bool backward) const;
    void replaceOne() const;
    void replaceAll() const;
    void scheduleRefresh() const;
    void refresh() const;
    void clearHighlights() const;
    void saveSettings() const;
};
