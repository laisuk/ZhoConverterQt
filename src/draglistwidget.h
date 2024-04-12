#ifndef DRAGLISTWIDGET_H
#define DRAGLISTWIDGET_H

#include <QListWidget>
#include <QDragEnterEvent>

class [[maybe_unused]] DragListWidget : public QListWidget {
Q_OBJECT

public:
    [[maybe_unused]] explicit DragListWidget(QWidget *parent = nullptr);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;

    void dropEvent(QDropEvent *event) override;

    [[nodiscard]] bool isItemInList(const QString &itemText) const;
};

#endif // DRAGLISTWIDGET_H
