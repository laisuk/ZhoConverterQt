#include "DictionaryWidget.h"
#include "DictionaryFile.h"
#include <QComboBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QHBoxLayout>

const QList<DictionarySlot> &dictionarySlots() {
    static const QList<DictionarySlot> definitions = {
        {.id = OPENCC_DICT_SLOT_ST_CHARACTERS, .name = "STCharacters"},
        {.id = OPENCC_DICT_SLOT_ST_PHRASES, .name = "STPhrases"},
        {.id = OPENCC_DICT_SLOT_TS_CHARACTERS, .name = "TSCharacters"},
        {.id = OPENCC_DICT_SLOT_TS_PHRASES, .name = "TSPhrases"},
        {.id = OPENCC_DICT_SLOT_TW_PHRASES, .name = "TWPhrases"},
        {.id = OPENCC_DICT_SLOT_TW_PHRASES_REV, .name = "TWPhrasesRev"},
        {.id = OPENCC_DICT_SLOT_HK_PHRASES, .name = "HKPhrases"},
        {.id = OPENCC_DICT_SLOT_HK_PHRASES_REV, .name = "HKPhrasesRev"},
        {.id = OPENCC_DICT_SLOT_TW_VARIANTS, .name = "TWVariants"},
        {.id = OPENCC_DICT_SLOT_TW_VARIANTS_PHRASES, .name = "TWVariantsPhrases"},
        {.id = OPENCC_DICT_SLOT_TW_VARIANTS_REV, .name = "TWVariantsRev"},
        {.id = OPENCC_DICT_SLOT_TW_VARIANTS_REV_PHRASES, .name = "TWVariantsRevPhrases"},
        {.id = OPENCC_DICT_SLOT_HK_VARIANTS, .name = "HKVariants"},
        {.id = OPENCC_DICT_SLOT_HK_VARIANTS_PHRASES, .name = "HKVariantsPhrases"},
        {.id = OPENCC_DICT_SLOT_HK_VARIANTS_REV, .name = "HKVariantsRev"},
        {.id = OPENCC_DICT_SLOT_HK_VARIANTS_REV_PHRASES, .name = "HKVariantsRevPhrases"},
        {.id = OPENCC_DICT_SLOT_JPS_CHARACTERS, .name = "JPShinjitaiCharacters"},
        {.id = OPENCC_DICT_SLOT_JPS_CHARACTERS_REV, .name = "JPShinjitaiCharactersRev"},
        {.id = OPENCC_DICT_SLOT_JPS_PHRASES, .name = "JPShinjitaiPhrases"},
        {.id = OPENCC_DICT_SLOT_ST_PUNCTUATIONS, .name = "STPunctuations"},
        {.id = OPENCC_DICT_SLOT_TS_PUNCTUATIONS, .name = "TSPunctuations"},
        {.id = OPENCC_DICT_SLOT_SEAL_CHARACTERS, .name = "SealCharacters"},
        {.id = OPENCC_DICT_SLOT_SEAL_CHARACTERS_REV, .name = "SealCharactersRev"},
        {.id = OPENCC_DICT_SLOT_SEAL_VARIANTS, .name = "SealVariants"},
        {.id = OPENCC_DICT_SLOT_SEAL_VARIANTS_REV, .name = "SealVariantsRev"},
    };
    return definitions;
}

DictionaryWidget::DictionaryWidget(QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    auto *header = new QHBoxLayout;
    // header->addWidget(new QLabel(tr("Custom Dictionary"), this));
    auto *title = new QLabel(tr("Custom Dictionary"), this);
    QFont headerFont = title->font();
    headerFont.setBold(true);
    headerFont.setPointSize(headerFont.pointSize() + 2);
    title->setFont(headerFont);
    header->addWidget(title);
    header->addStretch();
    status = new QLabel(this);
    status->setObjectName("dictionaryStatus");
    // status->setFrameStyle(QFrame::StyledPanel);
    // status->setMargin(6);
    status->setStyleSheet(R"(
        QLabel#dictionaryStatus {
            color: #0F6CBD;
            background-color: rgba(15, 108, 189, 20);
            border: 1px solid #0F6CBD;
            border-radius: 12px;
            padding: 4px 12px;
            font-weight: 600;
        }
    )");
    header->addWidget(status);
    layout->addLayout(header);
    auto *hint = new QLabel(
        tr(
            "Append merges mappings into a slot; Override replaces its mappings. Rows apply in order.\nUse UTF-8 text with source and target separated by a TAB; the first target is used.\nApplying with no configured dictionary files restores the default base dictionary."),
        this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    table = new QTableWidget(0, 4, this);
    table->setObjectName("dictionaryRows");
    table->setStyleSheet(R"(
        QTableWidget#dictionaryRows {
            border: 2px solid #B0B0B0;
        }
    )");
    table->setHorizontalHeaderLabels({tr("Slot"), tr("Mode"), tr("Dictionary file"), tr("Remove")});
    auto *horizontalHeader = table->horizontalHeader();

    horizontalHeader->setSectionResizeMode(QHeaderView::Interactive);
    horizontalHeader->setSectionResizeMode(2, QHeaderView::Stretch);
    table->setColumnWidth(0, 180); // Slot
    table->setColumnWidth(1, 100); // Mode
    table->setColumnWidth(3, 80); // Remove
    table->verticalHeader()->hide();
    layout->addWidget(table);
    empty = new QLabel(tr("No custom dictionaries configured."), this);
    layout->addWidget(empty);
    auto *actions = new QHBoxLayout;
    auto *add = new QPushButton(tr("✚ Add Custom Dictionary"), this);
    add->setObjectName("dictionaryAdd");
    apply = new QPushButton(tr("✔ Apply to Current Converter"), this);
    apply->setObjectName("dictionaryApply");

    // Bold both buttons
    QFont buttonFont = add->font();
    buttonFont.setBold(true);
    add->setFont(buttonFont);
    apply->setFont(buttonFont);

    actions->addWidget(add);
    actions->addStretch();
    actions->addWidget(apply);
    layout->addLayout(actions);
    connect(add, &QPushButton::clicked, this, [this] {
        addRow();
        saveRows();
    });
    connect(apply, &QPushButton::clicked, this, [this] { emit applyRequested(rows()); });
    QSettings settings;
    const int count = settings.beginReadArray("dictionary/rows");
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        addRow({
            .slot = settings.value("slot", OPENCC_DICT_SLOT_ST_CHARACTERS).toUInt(),
            .mode = settings.value("mode", OPENCC_CUSTOM_DICT_APPEND).toUInt(),
            .path = settings.value("path").toString()
        });
    }
    settings.endArray();
    setActiveSlotCount(0);
}

void DictionaryWidget::addRow(const DictionaryRow &row) {
    const int index = table->rowCount();
    table->insertRow(index);
    auto *slot = new QComboBox(table);
    for (const auto &[id, name]: dictionarySlots()) slot->addItem(QString::fromLatin1(name), id);
    if (slot->findData(row.slot) < 0) slot->addItem(tr("Invalid slot (%1)").arg(row.slot), row.slot);
    slot->setCurrentIndex(slot->findData(row.slot));
    auto *mode = new QComboBox(table);
    mode->addItem(tr("Append"), OPENCC_CUSTOM_DICT_APPEND);
    mode->addItem(tr("Override"), OPENCC_CUSTOM_DICT_OVERRIDE);
    if (mode->findData(row.mode) < 0) mode->addItem(tr("Invalid mode (%1)").arg(row.mode), row.mode);
    mode->setCurrentIndex(mode->findData(row.mode));
    auto *fileCell = new QWidget(table);
    auto *fileLayout = new QHBoxLayout(fileCell);
    fileLayout->setContentsMargins(0, 0, 0, 0);
    auto *path = new QLineEdit(row.path, fileCell);
    auto *browse = new QPushButton(tr("Browse…"), fileCell);
    fileLayout->addWidget(path);
    fileLayout->addWidget(browse);
    auto *remove = new QPushButton(tr("Remove"), table);
    remove->setStyleSheet("QPushButton { color: #D32F2F; }");
    table->setCellWidget(index, 0, slot);
    table->setCellWidget(index, 1, mode);
    table->setCellWidget(index, 2, fileCell);
    table->setCellWidget(index, 3, remove);
    connect(slot, &QComboBox::currentIndexChanged, this, [this] { saveRows(); });
    connect(mode, &QComboBox::currentIndexChanged, this, [this] { saveRows(); });
    connect(path, &QLineEdit::textChanged, this, [this] { saveRows(); });
    connect(browse, &QPushButton::clicked, this, [this, path] {
        if (const QString selected = QFileDialog::getOpenFileName(this, tr("Select dictionary"), path->text(),
                                                                  tr("Dictionary text (*.txt);;All files (*)")); !
            selected.isEmpty())
            path->setText(selected);
    });
    connect(remove, &QPushButton::clicked, this, [this, remove] {
        for (int i = 0; i < table->rowCount(); ++i)
            if (table->cellWidget(i, 3) == remove) {
                table->removeRow(i);
                break;
            }
        empty->setVisible(table->rowCount() == 0);
        saveRows();
    });
    empty->hide();
}

DictionaryRows DictionaryWidget::rows() const {
    DictionaryRows result;
    for (int i = 0; i < table->rowCount(); ++i) {
        result.append({
            .slot = qobject_cast<QComboBox *>(table->cellWidget(i, 0))->currentData().toUInt(),
            .mode = qobject_cast<QComboBox *>(table->cellWidget(i, 1))->currentData().toUInt(),
            .path = table->cellWidget(i, 2)->findChild<QLineEdit *>()->text()
        });
    }
    return result;
}

void DictionaryWidget::saveRows() const {
    QSettings settings;
    const auto definitions = rows();
    settings.beginWriteArray("dictionary/rows", static_cast<int>(definitions.size()));
    for (int i = 0; i < definitions.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("slot", definitions[i].slot);
        settings.setValue("mode", definitions[i].mode);
        settings.setValue("path", definitions[i].path);
    }
    settings.endArray();
}

void DictionaryWidget::setApplyEnabled(const bool enabled) const { apply->setEnabled(enabled); }

void DictionaryWidget::setActiveSlotCount(const int count) const {
    status->setText(count == 0 ? tr("Default dictionary") : tr("Custom dictionary (%1 slots)").arg(count));
}
