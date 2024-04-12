#include <QMimeData>
#include "draglistwidget.h"


[[maybe_unused]] DragListWidget::DragListWidget(QWidget *parent) : QListWidget(parent) {
    setSelectionMode(ExtendedSelection);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDragEnabled(true);
    setDropIndicatorShown(true);
    setDefaultDropAction(Qt::CopyAction);
    setDragDropMode(InternalMove);
}


void DragListWidget::dragEnterEvent(QDragEnterEvent *event) {
    const QMimeData *mimeData = event->mimeData();
    if (mimeData->hasUrls()) {
        event->acceptProposedAction();
    }
}

void DragListWidget::dropEvent(QDropEvent *event) {
    const QMimeData *mimeData = event->mimeData();
    if (event->mimeData()->hasUrls()) {
        QStringList fileUrls;
        for (const QUrl &url: mimeData->urls()) {
            fileUrls.append(url.toLocalFile());
        }

        for (const QString &fileUrl: fileUrls) {
            if (!isItemInList(fileUrl)) {
                auto *item = new QListWidgetItem(fileUrl);
                addItem(item);
            }
        }
        event->acceptProposedAction();
    }
}

bool DragListWidget::isItemInList(const QString &itemText) const {
    const QList<QListWidgetItem *> items = findItems(itemText, Qt::MatchExactly);
    return !items.isEmpty();
}
