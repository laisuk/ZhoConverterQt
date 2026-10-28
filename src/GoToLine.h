#pragma once

class QPlainTextEdit;
class QWidget;

/**
 * Opens a modal Go to Line prompt for a QPlainTextEdit.
 *
 * The current 1-based line number is preselected. Cancelling leaves the
 * editor unchanged; accepting moves its cursor to the requested line.
 */
class GoToLine final {
public:
    static void show(QPlainTextEdit *editor, QWidget *parent = nullptr);
};
