#include "GoToLine.h"

#include <QInputDialog>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QWidget>

void GoToLine::show(QPlainTextEdit *editor, QWidget *parent) {
    if (!editor)
        return;

    const int maxLine = editor->document()->blockCount();
    const int currentLine = editor->textCursor().blockNumber() + 1;

    const QString name = editor->objectName();
    const QString target =
            name == "tbSource"
                ? QObject::tr("Source")
                : name == "tbDestination"
                      ? QObject::tr("Destination")
                      : QObject::tr("Editor");

    QInputDialog dialog(parent ? parent : editor);
    dialog.setWindowTitle(QObject::tr("Go to Line - %1").arg(target));
    dialog.setLabelText(QObject::tr("Line number (1–%1):").arg(maxLine));
    dialog.setInputMode(QInputDialog::IntInput);
    dialog.setIntRange(1, maxLine);
    dialog.setIntStep(1);
    dialog.setIntValue(currentLine);

    // QInputDialog's integer spin box normally selects its text on focus.
    // Explicitly request selection after the dialog is shown by Qt.
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QTextBlock block = editor->document()->findBlockByNumber(dialog.intValue() - 1);
    if (!block.isValid())
        return;

    const QTextCursor cursor(block);
    editor->setTextCursor(cursor);
    editor->setFocus();
    editor->centerCursor();
}
