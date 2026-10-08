#pragma once
#include <QWidget>
#include <QList>
#include "OpenccFmmsegHelper.hpp"
class QTableWidget;
class QLabel;
class QPushButton;
struct DictionaryRow {
    opencc_dict_slot_t slot = OPENCC_DICT_SLOT_ST_CHARACTERS;
    opencc_custom_dict_mode_t mode = OPENCC_CUSTOM_DICT_APPEND;
    QString path;
};
using DictionaryRows = QList<DictionaryRow>;
Q_DECLARE_METATYPE(DictionaryRows)
struct DictionarySlot { opencc_dict_slot_t id; const char *name; };
const QList<DictionarySlot> &dictionarySlots();
class DictionaryWidget final : public QWidget {
    Q_OBJECT
public:
    explicit DictionaryWidget(QWidget *parent = nullptr);
    [[nodiscard]] DictionaryRows rows() const;
    void addRow(const DictionaryRow &row = {});
    void setApplyEnabled(bool enabled) const;
    void setActiveSlotCount(int count) const;
signals:
    void applyRequested(const DictionaryRows &rows);
private:
    void saveRows() const;
    QTableWidget *table;
    QLabel *status;
    QLabel *empty;
    QPushButton *apply;
};
