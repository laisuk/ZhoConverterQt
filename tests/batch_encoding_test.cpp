#include "mainwindow.h"
#include "FindReplaceDialog.h"
#include "texteditwidget.h"
#include <QTimer>
#include "DictionaryWidget.h"
#include "DictionaryFile.h"
#include <QLabel>
#include <QTableWidget>
#include <QStatusBar>
#include "batchworker.h"
#include "OpenccFmmsegHelper.hpp"
#include <QAction>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QFile>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>

namespace {
bool writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QString readText(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(file.readAll());
}
template<typename T> T *widget(MainWindow &window, const char *name) {
    auto *result = window.findChild<T *>(QString::fromLatin1(name));
    Q_ASSERT(result);
    return result;
}
}
class BatchEncodingTest final : public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
private slots:
    void initTestCase() const {
        QVERIFY(m_settings.isValid());
        QCoreApplication::setOrganizationName("ZhoBatchTests");
        QCoreApplication::setApplicationName("Encoding");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    static void init() { QSettings().clear(); }
    static void cstrAndViewConversion() {
        OpenccFmmsegHelper helper;
        const std::string text = QStringLiteral("汉字转换，龙马！").toStdString();
        constexpr opencc_config_t configs[] = {OPENCC_CONFIG_S2T, OPENCC_CONFIG_T2S, 0};
        for (const auto config : configs) {
            for (const bool punctuation : {false, true}) {
                QCOMPARE(helper.convert_cfg_cstr(text.c_str(), config, punctuation),
                         helper.convert_cfg(text, config, punctuation));
                QCOMPARE(helper.convert_cfg_cstr("", config, punctuation), std::string());
            }
        }
        // Neither the bounded view nor its backing array is NUL-terminated.
        constexpr char bytes[] = {'a', 'b', 'c', 'd'};
        QCOMPARE(helper.convert_cfg(std::string_view(bytes, 3), OPENCC_CONFIG_S2T), std::string("abc"));
        QCOMPARE(helper.convert_cfg(std::string_view(), OPENCC_CONFIG_S2T), std::string());
        const std::string embeddedNul("a\0b", 3);
        QCOMPARE(helper.convert_cfg_cstr(embeddedNul.c_str(), OPENCC_CONFIG_S2T),
                 helper.convert_cfg(embeddedNul, OPENCC_CONFIG_S2T));
    }
    static void findReplaceEditorSwitching() {
        QPlainTextEdit source, destination;
        source.setPlainText("cat cat");
        destination.setPlainText("cat destination cat");
        destination.setReadOnly(true);
        FindReplaceDialog dialog(&source);
        dialog.openFind(true);
        const auto edits = dialog.findChildren<QLineEdit *>();
        QCOMPARE(edits.size(), 2);
        edits[0]->setText("cat");
        edits[1]->setText("dog");
        QTRY_COMPARE(source.extraSelections().size(), 2);
        // Switch while a query refresh still holds a cursor into Source.
        edits[0]->setText("destination");
        dialog.setEditor(&destination);
        QVERIFY(source.extraSelections().isEmpty());
        QTRY_COMPARE(destination.extraSelections().size(), 1);
        auto *timer = dialog.findChild<QTimer *>();
        QVERIFY(timer);
        timer->stop();
        source.appendPlainText("cat");
        QVERIFY(!timer->isActive());
        destination.appendPlainText("destination");
        QVERIFY(timer->isActive());
        QTRY_COMPARE(destination.extraSelections().size(), 2);
        timer->stop();
        auto cursor = destination.textCursor();
        cursor.setPosition(1);
        destination.setTextCursor(cursor);
        QVERIFY(timer->isActive());
        for (auto *button : dialog.findChildren<QPushButton *>()) {
            if (button->text() == "Replace Next" || button->text() == "Replace All")
                QVERIFY(!button->isEnabled());
        }
        for (auto *editor : {&source, &destination, &source, &destination})
            dialog.setEditor(editor);
        timer->stop();
        source.appendPlainText("cat");
        QVERIFY(!timer->isActive());
        destination.appendPlainText("destination");
        QVERIFY(timer->isActive());
        dialog.close();
    }

    static void findReplaceReplacementLengths_data() {
        QTest::addColumn<QString>("replacement");
        QTest::newRow("shorter") << QString("x");
        QTest::newRow("longer") << QString("long replacement");
        QTest::newRow("empty") << QString();
        QTest::newRow("contains-query") << QString("catcat");
    }

    static void findReplaceReplacementLengths() {
        QFETCH(QString, replacement);
        TextEditWidget source;
        source.setObjectName("tbSource");
        source.setPlainText("cat cat cat");
        source.setLineNumbersVisible(true);
        FindReplaceDialog dialog(&source);
        dialog.openFind(true);
        const auto edits = dialog.findChildren<QLineEdit *>();
        edits[0]->setText("cat");
        edits[1]->setText(replacement);
        QTRY_COMPARE(source.extraSelections().size(), 3);
        QCOMPARE(source.extraSelections().first().format.background().color(), QColor(255, 214, 102, 125));
        QPushButton *next = nullptr;
        for (auto *button : dialog.findChildren<QPushButton *>())
            if (button->text() == "Replace Next") next = button;
        QVERIFY(next);
        next->click();
        QCOMPARE(source.toPlainText(), replacement + " cat cat");
        QCOMPARE(source.textCursor().selectionStart(), replacement.size() + 1);
        QCOMPARE(source.textCursor().selectedText(), QString("cat"));
        next->click();
        QCOMPARE(source.toPlainText(), replacement + " " + replacement + " cat");
        QCOMPARE(source.textCursor().selectionStart(), 2 * replacement.size() + 2);
        source.setLineNumbersVisible(false);
        QCOMPARE(source.textCursor().selectedText(), QString("cat"));
        QVERIFY(!source.extraSelections().isEmpty());
    }

    static void findReplaceEditorLifetime() {
        auto *source = new TextEditWidget;
        source->setPlainText("cat cat");
        FindReplaceDialog dialog(source);
        dialog.openFind(true);
        const auto edits = dialog.findChildren<QLineEdit *>();
        edits[0]->setText("cat");
        delete source; // Includes a pending query refresh and its document cursor.
        QVERIFY(!dialog.isVisible());
        edits[0]->setText("dog");
        dialog.updateTitle();
        dialog.openFind(true);
        QVERIFY(!dialog.isVisible());
        TextEditWidget destination;
        destination.setPlainText("dog dog");
        dialog.setEditor(&destination);
        dialog.openFind();
        QTRY_COMPARE(destination.extraSelections().size(), 2);
        dialog.close();
        QVERIFY(destination.extraSelections().isEmpty());
        {
            FindReplaceDialog temporary(&destination);
            temporary.openFind();
            temporary.findChildren<QLineEdit *>().first()->setText("dog");
            QTRY_COMPARE(destination.extraSelections().size(), 2);
        }
        QVERIFY(destination.extraSelections().isEmpty());
    }

    static void findReplaceFocusAndTabs() {
        MainWindow window;
        auto *source = widget<QPlainTextEdit>(window, "tbSource");
        auto *destination = widget<QPlainTextEdit>(window, "tbDestination");
        auto *tabs = widget<QTabWidget>(window, "tabWidget");
        auto *mainTab = widget<QWidget>(window, "tabMain");
        auto *find = widget<QAction>(window, "actionFind");
        auto *replace = widget<QAction>(window, "actionReplace");
        source->setPlainText("cat cat");
        destination->setPlainText("cat destination cat");
        window.show();
        window.activateWindow();
        destination->setFocus();
        QTRY_VERIFY(destination->hasFocus());
        replace->trigger();
        FindReplaceDialog *dialog = nullptr;
        for (auto *candidate : window.findChildren<QDialog *>())
            if (auto *match = dynamic_cast<FindReplaceDialog *>(candidate)) dialog = match;
        QVERIFY(dialog);
        const auto edits = dialog->findChildren<QLineEdit *>();
        QCOMPARE(edits.size(), 2);
        auto *query = edits[0];
        auto *replacement = edits[1];
        QPushButton *replaceAll = nullptr;
        for (auto *button : dialog->findChildren<QPushButton *>())
            if (button->text() == "Replace All") replaceAll = button;
        QVERIFY(replaceAll);
        query->setText("cat");
        replacement->setText("dog");
        QTRY_VERIFY(replaceAll->isEnabled());
        window.activateWindow();
        destination->setFocus();
        QTRY_VERIFY(destination->hasFocus());
        replaceAll->click();
        QCOMPARE(source->toPlainText(), QString("dog dog"));
        QCOMPARE(destination->toPlainText(), QString("cat destination cat"));
        find->trigger();
        QVERIFY(!replacement->isVisible());
        QVERIFY(!replaceAll->isEnabled());
        QTRY_COMPARE(destination->extraSelections().size(), 2);
        QVERIFY(source->extraSelections().isEmpty());
        replace->trigger();
        QVERIFY(replacement->isVisible());
        QCOMPARE(replacement->text(), QString("dog"));
        query->setText("dog");
        replacement->setText("fox");
        QTRY_VERIFY(replaceAll->isEnabled());
        replaceAll->click();
        QCOMPARE(source->toPlainText(), QString("fox fox"));
        QCOMPARE(destination->toPlainText(), QString("cat destination cat"));
        dialog->close();
        tabs->setCurrentIndex(1);
        find->trigger();
        QCOMPARE(tabs->currentWidget(), mainTab);
        dialog->close();
        tabs->setCurrentIndex(1);
        replace->trigger();
        QCOMPARE(tabs->currentWidget(), mainTab);
        QVERIFY(replacement->isVisible());
        dialog->close();
    }

    static void singleTextConversion() {
        MainWindow window;
        auto *source = widget<QPlainTextEdit>(window, "tbSource");
        auto *destination = widget<QPlainTextEdit>(window, "tbDestination");
        auto *process = widget<QPushButton>(window, "btnProcess");
        widget<QTabWidget>(window, "tabWidget")->setCurrentIndex(0);
        widget<QRadioButton>(window, "rbS2t")->setChecked(true);
        widget<QRadioButton>(window, "rbStd")->setChecked(true);
        destination->setPlainText("keep");
        process->click();
        QCOMPARE(destination->toPlainText(), QString("keep"));
        QVERIFY(window.statusBar()->currentMessage().contains("Source content is empty"));
        const QString text = QStringLiteral("汉字转换，龙马！");
        for (const bool punctuation : {false, true}) {
            OpenccFmmsegHelper helper;
            widget<QCheckBox>(window, "cbPunctuation")->setChecked(punctuation);
            source->setPlainText(text);
            process->click();
            QCOMPARE(destination->toPlainText(), QString::fromStdString(
                helper.convert_cfg(text.toStdString(), OPENCC_CONFIG_S2T, punctuation)));
            auto cursor = source->textCursor();
            cursor.setPosition(0);
            cursor.setPosition(2, QTextCursor::KeepAnchor);
            source->setTextCursor(cursor);
            process->click();
            QCOMPARE(destination->toPlainText(), QString::fromStdString(
                helper.convert_cfg(text.left(2).toStdString(), OPENCC_CONFIG_S2T, punctuation)));
            QVERIFY(window.statusBar()->currentMessage().contains("Selected text conversion completed"));
        }
        QTemporaryDir dir;
        const QString path = dir.filePath("custom.txt");
        QVERIFY(writeFile(path, QStringLiteral("龙\t魚\n").toUtf8()));
        window.findChild<DictionaryWidget *>()->addRow(
            {OPENCC_DICT_SLOT_ST_CHARACTERS, OPENCC_CUSTOM_DICT_APPEND, path});
        widget<QPushButton>(window, "dictionaryApply")->click();
        source->setPlainText(QStringLiteral("龙"));
        process->click();
        QCOMPARE(destination->toPlainText(), QStringLiteral("魚"));
    }
    static void dictionaryLoadingAndOrdering() {
        QTemporaryDir dir;
        const QString first = dir.filePath("first.txt"), second = dir.filePath("second.txt");
        QVERIFY(writeFile(first, QString::fromUtf8("\xef\xbb\xbf# comment\n龙\t𠀀 龍\n").toUtf8()));
        const auto pairs = loadDictionaryFile(first);
        QCOMPARE(pairs.size(), static_cast<size_t>(1));
        QCOMPARE(QString::fromStdString(pairs[0].target), QString::fromUtf8("𠀀"));
        QVERIFY(writeFile(second, QString::fromUtf8("龙\t魚\n").toUtf8()));
        OpenccFmmsegHelper converter({{OPENCC_DICT_SLOT_ST_CHARACTERS, OPENCC_CUSTOM_DICT_APPEND, pairs},
            {OPENCC_DICT_SLOT_ST_CHARACTERS, OPENCC_CUSTOM_DICT_APPEND, loadDictionaryFile(second)}});
        QCOMPARE(QString::fromStdString(converter.convert_cfg("龙", OPENCC_CONFIG_S2T)), QString::fromUtf8("魚"));
        OpenccFmmsegHelper overrideConverter({{OPENCC_DICT_SLOT_ST_CHARACTERS, OPENCC_CUSTOM_DICT_OVERRIDE, pairs}});
        QCOMPARE(QString::fromStdString(overrideConverter.convert_cfg("龙马", OPENCC_CONFIG_S2T)), QString::fromUtf8("𠀀马"));
        QVERIFY(writeFile(second, "missing separator\n"));
        QVERIFY_EXCEPTION_THROWN(loadDictionaryFile(second), std::runtime_error);
        QVERIFY(writeFile(second, QByteArray("a\t\xff\n")));
        QVERIFY_EXCEPTION_THROWN(loadDictionaryFile(second), std::runtime_error);
        QVERIFY(writeFile(second, "a\t\n"));
        QVERIFY_EXCEPTION_THROWN(loadDictionaryFile(second), std::runtime_error);
    }
    static void dictionaryApplyAndPersistence() {
        QTemporaryDir dir;
        const QString path = dir.filePath("custom.txt");
        QVERIFY(writeFile(path, QString::fromUtf8("龙\t魚\n").toUtf8()));
        MainWindow window;
        auto *dictionary = window.findChild<DictionaryWidget *>();
        QVERIFY(dictionary);
        auto *status = widget<QLabel>(window, "dictionaryStatus");
        auto *apply = widget<QPushButton>(window, "dictionaryApply");
        dictionary->addRow({OPENCC_DICT_SLOT_ST_CHARACTERS, OPENCC_CUSTOM_DICT_APPEND, path});
        QCOMPARE(status->text(), QString("Default dictionary"));
        apply->click();
        QCOMPARE(status->text(), QString("Custom dictionary (1 slots)"));
        QVERIFY(writeFile(path, "invalid"));
        apply->click();
        QCOMPARE(status->text(), QString("Custom dictionary (1 slots)"));
        QVERIFY(window.statusBar()->currentMessage().contains("Missing TAB"));
        auto *table = widget<QTableWidget>(window, "dictionaryRows");
        qobject_cast<QPushButton *>(table->cellWidget(0, 3))->click();
        QCOMPARE(status->text(), QString("Custom dictionary (1 slots)"));
        dictionary->addRow();
        apply->click();
        QCOMPARE(status->text(), QString("Default dictionary"));
        QSignalSpy requests(dictionary, &DictionaryWidget::applyRequested);
        apply->click();
        QCOMPARE(requests.count(), 1);
        widget<QPushButton>(window, "dictionaryAdd")->click();
        MainWindow restored;
        QCOMPARE(widget<QTableWidget>(restored, "dictionaryRows")->rowCount(), 2);
        QCOMPARE(widget<QLabel>(restored, "dictionaryStatus")->text(), QString("Default dictionary"));
    }
    static void dictionaryBatchGuard() {
        QTemporaryDir dir;
        const QString source = dir.filePath("source.txt");
        const QString dictionaryPath = dir.filePath("dictionary.txt");
        const QString out = dir.filePath("out");
        QVERIFY(QDir().mkpath(out));
        QVERIFY(writeFile(source, QString::fromUtf8("龙").toUtf8()));
        QVERIFY(writeFile(dictionaryPath, QString::fromUtf8("龙\t魚\n").toUtf8()));
        MainWindow window;
        auto *dictionary = window.findChild<DictionaryWidget *>();
        dictionary->addRow({OPENCC_DICT_SLOT_ST_CHARACTERS, OPENCC_CUSTOM_DICT_APPEND, dictionaryPath});
        auto *apply = widget<QPushButton>(window, "dictionaryApply");
        apply->click();
        widget<QListWidget>(window, "listSource")->addItem(source);
        widget<QLineEdit>(window, "lineEditDir")->setText(out);
        widget<QRadioButton>(window, "rbS2t")->setChecked(true);
        widget<QRadioButton>(window, "rbStd")->setChecked(true);
        widget<QTabWidget>(window, "tabWidget")->setCurrentIndex(1);
        widget<QPushButton>(window, "btnProcess")->click();
        QVERIFY(!apply->isEnabled());
        // Completion notification alone must not permit replacement.
        QVERIFY(QMetaObject::invokeMethod(&window, "onBatchFinished", Qt::DirectConnection, Q_ARG(bool, false)));
        QVERIFY(!apply->isEnabled());
        emit dictionary->applyRequested({});
        QCOMPARE(widget<QLabel>(window, "dictionaryStatus")->text(), QString("Custom dictionary (1 slots)"));
        QVERIFY(window.statusBar()->currentMessage().contains("batch thread"));
        QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 15000);
        QCOMPARE(readText(out + "/source_s2t.txt"), QString::fromUtf8("魚"));
        emit dictionary->applyRequested({});
        QCOMPARE(widget<QLabel>(window, "dictionaryStatus")->text(), QString("Default dictionary"));
    }
    static void batchTextEncodings_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("detect");
        QTest::newRow("empty-true") << QByteArray() << QString() << true;
        QTest::newRow("empty-false") << QByteArray() << QString() << false;
        QTest::newRow("big5-true") << QByteArray::fromHex("c163c5e9a4a4a4e5a4e5a672bd73bd58b4fab8d5a141b36fac4fa440ac71a5cea8d3bd54bb7ba6dbb0cab0bbb4faa55cafe0aabaa4e5b3b9a143b971b8a3a569a548a5bfbd54c5aaa8fac0c9aed7a4baae65a1430a")
            << QStringLiteral("繁體中文文字編碼測試，這是一段用來確認自動偵測功能的文章。電腦可以正確讀取檔案內容。\n") << true;
        QTest::newRow("big5-crlf-true") << QByteArray::fromHex("c163c5e9a4a4a4e5a4e5a672bd73bd58b4fab8d5a141b36fac4fa440ac71a5cea8d3bd54bb7ba6dbb0cab0bbb4faa55cafe0aabaa4e5b3b9a143b971b8a3a569a548a5bfbd54c5aaa8fac0c9aed7a4baae65a1430d0a")
            << QStringLiteral("繁體中文文字編碼測試，這是一段用來確認自動偵測功能的文章。電腦可以正確讀取檔案內容。\n") << true;
        QTest::newRow("gb18030-true") << QByteArray::fromHex("bcf2cce5d6d0cec4cec4d7d6b1e0c2ebb2e2cad4a3acd5e2cac7d2bbb6ced3c3c0b4c8b7c8cfd7d4b6afbcecb2e2b9a6c4dcb5c4cec4d5c2a1a3b5e7c4d4bfc9d2d4d5fdc8b7b6c1c8a1cec4bcfec4dac8dda1a30a")
            << QStringLiteral("简体中文文字编码测试，这是一段用来确认自动检测功能的文章。电脑可以正确读取文件内容。\n") << true;
        QTest::newRow("shift-jis-true") << QByteArray::fromHex("93fa967b8cea82cc95b68e9a8352815b836882f08ea993ae934982c994bb95ca82b582dc82b7814282b182ea82cd95b68fcd82f090b382b582ad93c782dd8d9e82de82bd82df82cc83658358836782c582b7814282d082e782aa82c882c6834a835e834a836982f08a6d944682b582dc82b781420a")
            << QStringLiteral("日本語の文字コードを自動的に判別します。これは文章を正しく読み込むためのテストです。ひらがなとカタカナを確認します。\n") << true;
        QTest::newRow("utf16le-bom-true") << QByteArray::fromHex("fffe496c575b6c8f62634b6dd58b0a00")
            << QStringLiteral("汉字转换测试\n") << true;
        QTest::newRow("utf16le-bom-false") << QByteArray::fromHex("fffe496c575b6c8f62634b6dd58b0a00")
            << QStringLiteral("汉字转换测试\n") << false;
        QTest::newRow("utf32le-bom-true") << QByteArray::fromHex("fffe0000496c0000575b00006c8f0000626300004b6d0000d58b00000a000000")
            << QStringLiteral("汉字转换测试\n") << true;
        QTest::newRow("utf32le-bom-false") << QByteArray::fromHex("fffe0000496c0000575b00006c8f0000626300004b6d0000d58b00000a000000")
            << QStringLiteral("汉字转换测试\n") << false;
        QTest::newRow("utf8-true") << QByteArray::fromHex("e6b189e5ad97e8bdace68da2e6b58be8af950a")
            << QStringLiteral("汉字转换测试\n") << true;
        QTest::newRow("utf8-false") << QByteArray::fromHex("e6b189e5ad97e8bdace68da2e6b58be8af950a")
            << QStringLiteral("汉字转换测试\n") << false;
    }

    static void batchTextEncodings() {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, text);
        QFETCH(bool, detect);
        QTemporaryDir temp;
        const QString input = temp.filePath("sample.txt");
        const QString out = temp.filePath("out");
        QVERIFY(writeFile(input, bytes));
        QVERIFY(QDir().mkpath(out));
        OpenccFmmsegHelper helper;
        BatchWorker batch({input}, out, &helper, OpenccFmmsegHelper::config_name_to_id("s2t"), false, false, false, true, false, detect);
        QSignalSpy errors(&batch, &BatchWorker::error);
        batch.process();
        QCOMPARE(errors.count(), 0);
        OpenccFmmsegHelper converter;
        QCOMPARE(readText(out + "/sample_s2t.txt"),
                 QString::fromStdString(converter.convert(text.toStdString(), "s2t")));
    }

    static void batchEncodingSettingAndFailure() {
        QTemporaryDir temp;
        const QString out = temp.filePath("out");
        QVERIFY(QDir().mkpath(out));
        const QString path = temp.filePath("invalid.txt");
        QVERIFY(writeFile(path, QByteArray::fromHex("ff")));
        const QString target = out + "/invalid_s2t.txt";
        QVERIFY(writeFile(target, "existing output"));
        OpenccFmmsegHelper helper;
        BatchWorker batch({path}, out, &helper, OpenccFmmsegHelper::config_name_to_id("s2t"), false, false, false, true, false, true);
        QSignalSpy errors(&batch, &BatchWorker::error);
        batch.process();
        QCOMPARE(errors.count(), 1);
        QCOMPARE(readText(target), QStringLiteral("existing output"));
        MainWindow window;
        auto *action = widget<QAction>(window, "actionAutoDetectCjkBatch");
        QVERIFY(action->isCheckable());
        QVERIFY(!action->isChecked());
        auto *menu = widget<QMenu>(window, "menuSettings");
        QCOMPARE(menu->actions().at(1), action);
        action->setChecked(true);
        QCOMPARE(QSettings().value("batch/autoDetectCjkEncoding").toBool(), true);
        MainWindow restored;
        QVERIFY(widget<QAction>(restored, "actionAutoDetectCjkBatch")->isChecked());
        // Exercise the UI-to-worker option snapshot using a legacy CJK file.
        const QString legacy = temp.filePath("legacy.txt");
        QVERIFY(writeFile(legacy, QByteArray::fromHex("c163c5e9a4a4a4e5a4e5a672bd73bd58b4fab8d5a141b36fac4fa440ac71a5cea8d3bd54bb7ba6dbb0cab0bbb4faa55cafe0aabaa4e5b3b9a143b971b8a3a569a548a5bfbd54c5aaa8fac0c9aed7a4baae65a1430a")));
        widget<QListWidget>(restored, "listSource")->addItem(legacy);
        widget<QLineEdit>(restored, "lineEditDir")->setText(out);
        widget<QRadioButton>(restored, "rbS2t")->setChecked(true);
        widget<QRadioButton>(restored, "rbStd")->setChecked(true);
        widget<QTabWidget>(restored, "tabWidget")->setCurrentIndex(1);
        widget<QPushButton>(restored, "btnProcess")->click();
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(out + "/legacy_s2t.txt"), 15000);
        OpenccFmmsegHelper converter;
        QTRY_COMPARE(readText(out + "/legacy_s2t.txt"),
                 QString::fromStdString(converter.convert(QStringLiteral("繁體中文文字編碼測試，這是一段用來確認自動偵測功能的文章。電腦可以正確讀取檔案內容。\n").toStdString(), "s2t")));
    }

};
QTEST_MAIN(BatchEncodingTest)
#include "batch_encoding_test.moc"
