#include "filetype_utils.h"

#include <QDir>
#include <unordered_set>
#include <string>

#include "OpenccFmmsegHelper.hpp"
#include "opencc_fmmseg_capi.h"

namespace {

    inline const std::unordered_set<std::string> TEXTFILE_EXTENSIONS = {
        "txt", "md", "rst",
        "html", "htm", "xhtml", "xml",
        "json", "yml", "yaml", "ini", "cfg", "toml",
        "csv", "tsv",
        "c", "cpp", "cc", "cxx", "h", "hpp",
        "cs", "java", "kt", "kts",
        "py", "rb", "go", "rs", "swift",
        "js", "mjs", "cjs", "ts", "tsx", "jsx",
        "sh", "bash", "zsh", "ps1", "cmd", "bat",
        "gradle", "cmake", "make", "mak", "ninja",
        "tex", "bib", "log",
        "srt", "vtt", "ass", "ttml2"
    };

    inline const std::unordered_set<std::string> OFFICE_EXTENSIONS = {
        "docx", "xlsx", "pptx", "odt", "ods", "odp", "epub"
        // add more if needed
    };

} // anonymous namespace (internal constants only)

bool isOfficeExt(const QString &extLower)
{
    return OFFICE_EXTENSIONS.count(extLower.toStdString()) != 0;
}

bool isTextExt(const QString &extLower)
{
    return TEXTFILE_EXTENSIONS.count(extLower.toStdString()) != 0;
}

bool isAllowedTextLike(const QString &extLower)
{
    // allow files with NO extension as text
    return extLower.isEmpty() || isTextExt(extLower);
}

QString makeOutputPath(const QString &outDir,
                       const QString &baseName,
                       const opencc_config_t &config,
                       const QString &extLower)
{
    const QString fileName =
        baseName + "_" + OpenccFmmsegHelper::config_id_to_name(config).data() +
        (extLower.isEmpty() ? QString() : "." + extLower);

    return QDir(outDir).filePath(fileName);
}
