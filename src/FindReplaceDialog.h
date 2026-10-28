#pragma once

#include <QDialog>
#include <QPointer>
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

    void setEditor(QPlainTextEdit *editor);

    void updateTitle();

    void openFind(bool replaceMode = false);

protected:
    bool event(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QPlainTextEdit *m_editor;
    QPointer<QPlainTextEdit> m_styledEditor;
    QString m_originalEditorStyleSheet;
    void updateSelectionStyle(bool active);
    QMetaObject::Connection m_contentsConnection;
    QMetaObject::Connection m_cursorConnection;
    QMetaObject::Connection m_destroyedConnection;
    void connectEditor();
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
    mutable QTextCursor m_queryStart;
    bool m_replaceVisible = false;

    [[nodiscard]] QTextDocument::FindFlags flags(bool backward = false) const;
    [[nodiscard]] QRegularExpression expression() const;
    [[nodiscard]] QTextCursor search(const QTextCursor &from, bool backward = false) const;
    [[nodiscard]] bool validQuery() const;
    [[nodiscard]] bool isCurrentMatch() const;
    void navigate(bool backward) const;
    void replaceNext() const;
    void replaceAll() const;
    void queryChanged() const;
    void scheduleRefresh() const;
    void refresh() const;
    void clearHighlights() const;
    void saveSettings() const;
};
