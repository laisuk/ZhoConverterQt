#include <QListWidgetItem>
#include <QFile>
#include <QTextStream>
#include <QIODevice>
#include <QMimeData>
#include "texteditwidget.h"


TextEditWidget::TextEditWidget(QWidget *parent) : QPlainTextEdit(parent) {
    setAcceptDrops(true);
}

void TextEditWidget::dragEnterEvent(QDragEnterEvent *event) {
    if (const QMimeData *mimeData = event->mimeData(); mimeData->hasUrls() || mimeData->hasText()) {
        event->acceptProposedAction();
    }
}

void TextEditWidget::dropEvent(QDropEvent *event) {
    if (const QMimeData *mimeData = event->mimeData(); mimeData->hasUrls()) {
        const QString filePath = mimeData->urls().at(0).toLocalFile();
        loadFile(filePath);
        contentFilename = filePath;
        emit fileDropped(filePath);
    } else if (mimeData->hasText()) {
        document()->setPlainText(mimeData->text());
        contentFilename = "";
        emit fileDropped(QString{});
    }
}

void TextEditWidget::loadFile(const QString &filePath) const {
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&file);
        document()->setPlainText(in.readAll());
        file.close();
    } else {
        document()->setPlainText("Error loading file: " + file.errorString());
    }
}
