#include "FindReplaceDialog.h"

#include <QCheckBox>
#include <QColor>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTextDocument>
#include <QTimer>
#include <QVBoxLayout>

namespace {
    constexpr int HighlightLimit = 1000;
    constexpr int CountLimit = 100000;
}

FindReplaceDialog::FindReplaceDialog(QPlainTextEdit *editor, QWidget *parent)
    : QDialog(parent), m_editor(editor) {
    Q_ASSERT(editor);
    // setWindowTitle(tr("Find / Replace"));
    updateTitle();
    setWindowFlag(Qt::Tool);
    setModal(false);
    setMinimumWidth(470);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    auto *findRow = new QHBoxLayout;
    m_find = new QLineEdit(this);
    m_find->setPlaceholderText(tr("Find in editor…"));
    m_find->setClearButtonEnabled(true);
    m_prev = new QPushButton(tr("↑"), this);
    m_next = new QPushButton(tr("↓"), this);
    m_prev->setToolTip(tr("Previous match (Shift+F3)"));
    m_next->setToolTip(tr("Next match (F3 / Enter)"));
    m_prev->setFixedWidth(35);
    m_next->setFixedWidth(35);
    m_counter = new QLabel(tr("0 matches"), this);
    m_counter->setMinimumWidth(75);
    findRow->addWidget(m_find, 1);
    findRow->addWidget(m_prev);
    findRow->addWidget(m_next);
    findRow->addWidget(m_counter);
    layout->addLayout(findRow);

    m_replaceRow = new QWidget(this);
    auto *replaceLayout = new QHBoxLayout(m_replaceRow);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    m_replace = new QLineEdit(m_replaceRow);
    m_replace->setPlaceholderText(tr("Replace with…"));
    m_replaceOne = new QPushButton(tr("Replace"), m_replaceRow);
    m_replaceAll = new QPushButton(tr("Replace All"), m_replaceRow);
    replaceLayout->addWidget(m_replace, 1);
    replaceLayout->addWidget(m_replaceOne);
    replaceLayout->addWidget(m_replaceAll);
    layout->addWidget(m_replaceRow);

    auto *options = new QHBoxLayout;
    m_case = new QCheckBox(tr("Match case"), this);
    m_words = new QCheckBox(tr("Whole words"), this);
    m_regex = new QCheckBox(tr("Regex"), this);
    options->addWidget(m_case);
    options->addWidget(m_words);
    options->addWidget(m_regex);
    options->addStretch();
    layout->addLayout(options);
    m_message = new QLabel(this);
    m_message->setWordWrap(true);
    layout->addWidget(m_message);

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setSingleShot(true);
    m_refreshTimer->setInterval(120);

    connect(m_find, &QLineEdit::textChanged, this, [this] { queryChanged(); });
    connect(m_case, &QCheckBox::toggled, this, [this] { queryChanged(); });
    connect(m_words, &QCheckBox::toggled, this, [this] { queryChanged(); });
    connect(m_regex, &QCheckBox::toggled, this, [this] { queryChanged(); });
    connect(m_refreshTimer, &QTimer::timeout, this, [this] { refresh(); });
    connect(m_prev, &QPushButton::clicked, this, [this] { navigate(true); });
    connect(m_next, &QPushButton::clicked, this, [this] { navigate(false); });
    connect(m_replaceOne, &QPushButton::clicked, this, [this] { replaceOne(); });
    connect(m_replaceAll, &QPushButton::clicked, this, [this] { replaceAll(); });
    connect(m_find, &QLineEdit::returnPressed, this, [this] { navigate(false); });
    connectEditor();

    const QSettings settings;
    m_case->setChecked(settings.value("findReplace/matchCase", false).toBool());
    m_words->setChecked(settings.value("findReplace/wholeWords", false).toBool());
    m_regex->setChecked(settings.value("findReplace/regex", false).toBool());
    m_replaceRow->hide();
}

FindReplaceDialog::~FindReplaceDialog() { saveSettings(); }

void FindReplaceDialog::connectEditor() {
    m_contentsConnection = connect(m_editor->document(), &QTextDocument::contentsChanged,
                                   this, [this] { if (isVisible()) scheduleRefresh(); });
    m_cursorConnection = connect(m_editor, &QPlainTextEdit::cursorPositionChanged,
                                 this, [this] { if (isVisible()) scheduleRefresh(); });
    m_destroyedConnection = connect(m_editor, &QObject::destroyed, this, [this] {
        m_editor = nullptr;
        m_queryStart = QTextCursor{};
        close();
    });
}

void FindReplaceDialog::setEditor(QPlainTextEdit *editor) {
    Q_ASSERT(editor);
    if (!editor || m_editor == editor) return;
    clearHighlights();
    disconnect(m_contentsConnection);
    disconnect(m_cursorConnection);
    disconnect(m_destroyedConnection);
    m_refreshTimer->stop();
    m_queryStart = QTextCursor{};
    m_editor = editor;
    updateTitle();
    connectEditor();
    refresh();
}

void FindReplaceDialog::updateTitle() {
    const QString target = m_editor->objectName() == "tbSource"
                               ? tr("Source")
                               : tr("Destination");
    setWindowTitle(tr("Find / Replace - %1").arg(target));
}

void FindReplaceDialog::openFind(const bool replaceMode) {
    m_replaceVisible = replaceMode;
    m_replaceRow->setVisible(replaceMode);
    if (const QTextCursor cursor = m_editor->textCursor(); cursor.hasSelection() && !cursor.selectedText().contains(
                                                               QChar::ParagraphSeparator)
                                                           && cursor.selectedText().size() <= 200) {
        m_find->setText(cursor.selectedText());
    }
    show();
    raise();
    activateWindow();
    m_find->setFocus();
    m_find->selectAll();
    navigate(false);
    scheduleRefresh();
}

QTextDocument::FindFlags FindReplaceDialog::flags(const bool backward) const {
    QTextDocument::FindFlags result;
    if (m_case->isChecked()) result |= QTextDocument::FindCaseSensitively;
    if (m_words->isChecked()) result |= QTextDocument::FindWholeWords;
    if (backward) result |= QTextDocument::FindBackward;
    return result;
}

QRegularExpression FindReplaceDialog::expression() const {
    QRegularExpression re(m_find->text());
    if (!m_case->isChecked())
        re.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    return re;
}

bool FindReplaceDialog::validQuery() const {
    return !m_find->text().isEmpty() && (!m_regex->isChecked() || expression().isValid());
}

QTextCursor FindReplaceDialog::search(const QTextCursor &from, const bool backward) const {
    if (!validQuery()) return {};
    if (m_regex->isChecked())
        return m_editor->document()->find(expression(), from, flags(backward));
    return m_editor->document()->find(m_find->text(), from, flags(backward));
}

bool FindReplaceDialog::isCurrentMatch() const {
    if (!validQuery()) return false;
    const QTextCursor selected = m_editor->textCursor();
    if (!selected.hasSelection()) return false;
    QTextCursor start(m_editor->document());
    start.setPosition(selected.selectionStart());
    const QTextCursor match = search(start);
    return !match.isNull() && match.selectionStart() == selected.selectionStart()
           && match.selectionEnd() == selected.selectionEnd();
}

void FindReplaceDialog::navigate(const bool backward) const {
    m_queryStart = QTextCursor{};
    if (!validQuery()) return;
    QTextCursor start = m_editor->textCursor();
    start.setPosition(backward ? start.selectionStart() : start.selectionEnd());
    QTextCursor found = search(start, backward);
    if (found.isNull()) {
        QTextCursor edge(m_editor->document());
        edge.movePosition(backward ? QTextCursor::End : QTextCursor::Start);
        found = search(edge, backward);
    }
    if (!found.isNull()) {
        m_editor->setTextCursor(found);
        m_editor->ensureCursorVisible();
    }
    refresh();
}

void FindReplaceDialog::replaceOne() const {
    if (!validQuery() || m_editor->isReadOnly()) return;
    if (!isCurrentMatch()) {
        navigate(false);
        return;
    }
    QTextCursor cursor = m_editor->textCursor();
    cursor.insertText(m_replace->text());
    m_editor->setTextCursor(cursor);
    navigate(false);
}

void FindReplaceDialog::replaceAll() const {
    if (!validQuery() || m_editor->isReadOnly()) return;
    QTextCursor scan(m_editor->document());
    scan.movePosition(QTextCursor::Start);
    QTextCursor edit(m_editor->document());
    edit.beginEditBlock();
    int replaced = 0;
    while (true) {
        QTextCursor found = search(scan);
        if (found.isNull()) break;
        const int start = found.selectionStart();
        if (const int end = found.selectionEnd(); start == end) {
            // Avoid infinite loops on zero-width regex matches.
            scan.setPosition(end);
            if (!scan.movePosition(QTextCursor::NextCharacter)) break;
            continue;
        }
        found.insertText(m_replace->text());
        ++replaced;
        scan.setPosition(static_cast<int>(start + m_replace->text().size()));
    }
    edit.endEditBlock();
    m_message->setText(tr("Replaced %1 occurrence(s).").arg(replaced));
    refresh();
}

void FindReplaceDialog::queryChanged() const {
    if (m_queryStart.isNull()) {
        m_queryStart = m_editor->textCursor();
        // Keep the same starting point while refining a selected match.
        m_queryStart.setPosition(m_queryStart.selectionStart());
    }
    scheduleRefresh();
}

void FindReplaceDialog::scheduleRefresh() const { m_refreshTimer->start(); }

void FindReplaceDialog::refresh() const {
    if (!m_editor) return;
    clearHighlights();
    const bool valid = validQuery();
    if (valid && !m_queryStart.isNull()) {
        const QTextCursor start = m_queryStart;
        m_queryStart = QTextCursor{};
        QTextCursor found = search(start);
        if (found.isNull())
            found = search(QTextCursor(m_editor->document()));
        if (!found.isNull()) {
            m_editor->setTextCursor(found);
            m_editor->ensureCursorVisible();
        }
    }
    m_prev->setEnabled(valid);
    m_next->setEnabled(valid);
    m_replaceOne->setEnabled(valid && !m_editor->isReadOnly());
    m_replaceAll->setEnabled(valid && !m_editor->isReadOnly());
    if (!valid) {
        m_counter->setText(tr("0 matches"));
        m_message->setText(m_regex->isChecked() && !m_find->text().isEmpty()
                               ? expression().errorString()
                               : QString{});
        return;
    }

    QList<QTextEdit::ExtraSelection> highlights;
    QTextCursor scan(m_editor->document());
    scan.movePosition(QTextCursor::Start);
    const QTextCursor current = m_editor->textCursor();
    int total = 0;
    int currentIndex = 0;
    while (total < CountLimit) {
        QTextCursor found = search(scan);
        if (found.isNull()) break;
        const int start = found.selectionStart();
        const int end = found.selectionEnd();
        if (start == end) {
            scan.setPosition(end);
            if (!scan.movePosition(QTextCursor::NextCharacter)) break;
            continue;
        }
        ++total;
        if (current.hasSelection() && start == current.selectionStart()
            && end == current.selectionEnd())
            currentIndex = total;
        if (highlights.size() < HighlightLimit) {
            QTextEdit::ExtraSelection selection;
            selection.cursor = found;
            selection.format.setBackground(QColor(255, 214, 102, 125));
            highlights.append(selection);
        }
        scan.setPosition(end);
    }
    m_editor->setExtraSelections(highlights);
    const QString count = total == CountLimit
                              ? tr("%1+ matches").arg(CountLimit)
                              : currentIndex
                                    ? tr("%1 of %2").arg(currentIndex).arg(total)
                                    : tr("%1 matches").arg(total);
    m_counter->setText(count);
    m_message->setText(total > HighlightLimit
                           ? tr("Highlighting the first %1 matches.").arg(HighlightLimit)
                           : QString{});
}

void FindReplaceDialog::clearHighlights() const {
    if (m_editor) m_editor->setExtraSelections({});
}

void FindReplaceDialog::saveSettings() const {
    QSettings settings;
    settings.setValue("findReplace/matchCase", m_case->isChecked());
    settings.setValue("findReplace/wholeWords", m_words->isChecked());
    settings.setValue("findReplace/regex", m_regex->isChecked());
}

void FindReplaceDialog::closeEvent(QCloseEvent *event) {
    m_refreshTimer->stop();
    clearHighlights();
    saveSettings();
    QDialog::closeEvent(event);
}

void FindReplaceDialog::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_F3) {
        navigate(event->modifiers().testFlag(Qt::ShiftModifier));
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}
