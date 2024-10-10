#ifndef DRAGLISTWIDGET_H
#define DRAGLISTWIDGET_H

#include <QListWidget>
#include <QDragEnterEvent>

class DragListWidget : public QListWidget {
Q_OBJECT

public:
    explicit DragListWidget(QWidget *parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;

    void dropEvent(QDropEvent *event) override;

    bool isItemInList(const QString &itemText) const;
};

#endif // DRAGLISTWIDGET_H
