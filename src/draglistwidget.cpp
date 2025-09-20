#include <QMimeData>
#include "draglistwidget.h"


DragListWidget::DragListWidget(QWidget* parent) : QListWidget(parent)
{
    setSelectionMode(ExtendedSelection);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDragEnabled(true);
    setDropIndicatorShown(true);
    setDefaultDropAction(Qt::CopyAction);
    setDragDropMode(InternalMove);
}


void DragListWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (const QMimeData* mimeData = event->mimeData(); mimeData->hasUrls())
    {
        event->acceptProposedAction();
    }
}

void DragListWidget::dropEvent(QDropEvent* event)
{
    const QMimeData* mimeData = event->mimeData();
    if (event->mimeData()->hasUrls())
    {
        for (const QUrl& url : mimeData->urls())
        {
            if (const QString path = url.toLocalFile(); !path.isEmpty() && !isItemInList(path))
            {
                // Let QListWidget allocate & own the item internally
                addItem(path);
            }
        }
        event->acceptProposedAction();
    }
}

bool DragListWidget::isItemInList(const QString& itemText) const
{
    const QList<QListWidgetItem*> items = findItems(itemText, Qt::MatchExactly);
    return !items.isEmpty();
}
