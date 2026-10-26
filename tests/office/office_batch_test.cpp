#include "office_test_support.hpp"
#include "opencc-fmmseg/OpenccFmmsegHelper.hpp"
#include "pdfium/batchworker.h"
#include "src/filetype_utils.h"
#include <QCoreApplication>
#include <QTemporaryDir>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir temp;
        require(temp.isValid(), "temporary directory");
        OpenccFmmsegHelper helper(
            {
                {
                    .slot = OPENCC_DICT_SLOT_ST_CHARACTERS, .mode = OPENCC_CUSTOM_DICT_APPEND,
                    .pairs = {{.source = "龙", .target = "魚"}}
                }
            });
        const QString stem = QString::fromUtf8("龙汉“字”");
        const QString inPath = temp.filePath(stem + ".docx");
        const auto asPath = [](const QString &text) {
            const auto bytes = text.toUtf8();
            return fs::path(std::u8string(bytes.begin(), bytes.end()));
        };
        const auto file = asPath(inPath);
        const std::string body = "<w:t>龙汉“字”</w:t>";
        const auto bytes = Access::make({{"word/document.xml", "<w:rFonts w:ascii=\"龙汉\"/>" + body}});
        write(file, bytes);
        for (bool punctuation: {false, true}) {
            for (bool filename: {false, true}) {
                BatchWorker worker({inPath}, temp.path(), &helper, OPENCC_CONFIG_S2T, punctuation, filename,
                                   false, false, false);
                int progress = 0, finished = 0, errors = 0;
                QStringList logs;
                QObject::connect(&worker, &BatchWorker::progress, [&](const int current, const int total) {
                    if (current == 1 && total == 1)
                        ++progress;
                });
                QObject::connect(&worker, &BatchWorker::finished, [&](const bool cancelled) {
                    if (!cancelled)
                        ++finished;
                });
                QObject::connect(&worker, &BatchWorker::error, [&](const QString &) { ++errors; });
                QObject::connect(&worker, &BatchWorker::log, [&](const QString &line) { logs << line; });
                worker.process();
                const QString convertedStem =
                        filename
                            ? QString::fromStdString(helper.convert_cfg_cstr(stem.toUtf8().constData(),
                                                                             OPENCC_CONFIG_S2T, false))
                            : stem;
                const auto out = makeOutputPath(temp.path(), convertedStem, OPENCC_CONFIG_S2T, "docx");
                const auto result = Access::unpack(read(asPath(out)));
                const auto expected = "<w:rFonts w:ascii=\"龙汉\"/>" +
                                      helper.convert_cfg_cstr(body.c_str(), OPENCC_CONFIG_S2T, punctuation);
                require(result[0].second == expected, "BatchWorker settings/custom dictionaries/fonts");
                require(progress == 1 && finished == 1 && errors == 0 &&
                        logs.join("\n").contains("Conversion completed"),
                        "batch signals/logs");
            }
        }
        BatchWorker cancelled({inPath}, temp.path(), &helper, OPENCC_CONFIG_S2T, false, false, false, false,
                              false);
        bool cancelledSignal = false;
        QObject::connect(&cancelled, &BatchWorker::finished, [&](const bool value) { cancelledSignal = value; });
        cancelled.requestCancel();
        cancelled.process();
        require(cancelledSignal, "batch cancellation");
        std::cout << "Office BatchWorker integration checks passed.\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
