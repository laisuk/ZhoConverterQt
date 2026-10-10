#include "OpenXmlHelper.h"

#include <QByteArray>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QVector>

#include <zip.h>
#include <libxml/parser.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {
    constexpr auto W_NS = "http://schemas.openxmlformats.org/wordprocessingml/2006/main";
    constexpr auto TEXT_NS = "urn:oasis:names:tc:opendocument:xmlns:text:1.0";
    constexpr auto TABLE_NS = "urn:oasis:names:tc:opendocument:xmlns:table:1.0";

    struct ZipCloser {
        void operator()(zip_t *p) const { if (p) zip_close(p); }
    };

    struct XmlCloser {
        void operator()(xmlDoc *p) const { if (p) xmlFreeDoc(p); }
    };

    struct ZipFileCloser {
        void operator()(zip_file_t *p) const { if (p) zip_fclose(p); }
    };

    using ZipPtr = std::unique_ptr<zip_t, ZipCloser>;
    using XmlPtr = std::unique_ptr<xmlDoc, XmlCloser>;
    using ZipFilePtr = std::unique_ptr<zip_file_t, ZipFileCloser>;

    ZipPtr openZip(const QString &path) {
        int error = 0;
        ZipPtr zip(zip_open(QFile::encodeName(path).constData(), ZIP_RDONLY, &error));
        if (!zip) throw std::runtime_error("Cannot open document archive");
        return zip;
    }

    bool hasEntry(zip_t *zip, const QByteArray &name) {
        return zip_name_locate(zip, name.constData(), 0) >= 0;
    }

    QByteArray readEntry(zip_t *zip, const QByteArray &name) {
        const zip_int64_t index = zip_name_locate(zip, name.constData(), 0);
        if (index < 0) throw std::runtime_error("Document archive entry not found");
        const ZipFilePtr file(zip_fopen_index(zip, static_cast<zip_uint64_t>(index), 0));
        if (!file) throw std::runtime_error("Cannot open document archive entry");
        QByteArray out;
        char buffer[65536];
        for (;;) {
            const zip_int64_t n = zip_fread(file.get(), buffer, sizeof(buffer));
            if (n < 0) throw std::runtime_error("Cannot decompress document archive entry");
            if (n == 0) break;
            if (n > std::numeric_limits<qsizetype>::max() - out.size())
                throw std::runtime_error("Document archive entry too large");
            out.append(buffer, static_cast<qsizetype>(n));
        }
        return out;
    }

    XmlPtr parseXml(const QByteArray &bytes) {
        if (bytes.size() > std::numeric_limits<int>::max())
            throw std::runtime_error("XML entry too large");
        XmlPtr doc(xmlReadMemory(bytes.constData(), static_cast<int>(bytes.size()),
                                 nullptr, nullptr,
                                 XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING));
        if (!doc) throw std::runtime_error("Cannot parse document XML");
        return doc;
    }

    bool inNs(const xmlNode *node, const char *ns) {
        return node && node->type == XML_ELEMENT_NODE && node->ns &&
               xmlStrEqual(node->ns->href, reinterpret_cast<const xmlChar *>(ns));
    }

    bool named(const xmlNode *node, const char *ns, const char *local) {
        return inNs(node, ns) &&
               xmlStrEqual(node->name, reinterpret_cast<const xmlChar *>(local));
    }

    QString attribute(const xmlNode *node, const char *ns, const char *local) {
        xmlChar *value = xmlGetNsProp(node, reinterpret_cast<const xmlChar *>(local),
                                      reinterpret_cast<const xmlChar *>(ns));
        if (!value)
            value = xmlGetProp(node, reinterpret_cast<const xmlChar *>(local));
        const QString result = value
                                   ? QString::fromUtf8(reinterpret_cast<const char *>(value))
                                   : QString{};
        if (value) xmlFree(value);
        return result;
    }

    QString textOf(const xmlNode *node) {
        return node->content
                   ? QString::fromUtf8(reinterpret_cast<const char *>(node->content))
                   : QString{};
    }

    void walk(xmlNode *node, const std::function<void(xmlNode *, bool)> &fn) {
        for (; node; node = node->next) {
            if (node->type == XML_ELEMENT_NODE) fn(node, true);
            if (node->children) walk(node->children, fn);
            if (node->type == XML_ELEMENT_NODE) fn(node, false);
        }
    }

    void normalizeNewlines(QString &text) {
        text.replace("\r\n", "\n");
        text.replace('\r', '\n');
    }

    void trimTrailingNewlines(QString &text) {
        while (text.endsWith('\n') || text.endsWith('\r')) text.chop(1);
    }

    QString letters(int value, const bool upper) {
        value = std::max(value, 1);
        QString result;
        while (value > 0) {
            --value;
            result.prepend(QChar((upper ? 'A' : 'a') + value % 26));
            value /= 26;
        }
        return result;
    }

    QString roman(int value) {
        value = std::max(value, 1);
        static constexpr std::array<std::pair<int, const char *>, 13> mapping{
            {
                {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"},
                {100, "C"}, {90, "XC"}, {50, "L"}, {40, "XL"},
                {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}
            }
        };
        QString out;
        for (const auto &[n, s]: mapping)
            while (value >= n) {
                out += QLatin1String(s);
                value -= n;
            }
        return out;
    }

    QString formatCounter(int value, const QString &fmt) {
        value = std::max(value, 1);
        if (fmt == "lowerLetter") return letters(value, false);
        if (fmt == "upperLetter") return letters(value, true);
        if (fmt == "lowerRoman") return roman(value).toLower();
        if (fmt == "upperRoman") return roman(value);
        if (fmt == "decimalZero" && value < 10) return "0" + QString::number(value);
        return QString::number(value);
    }

    QString bulletGlyph(QString text, const QString &font) {
        text.remove('\t');
        text.replace(QChar(0xa0), ' ');
        text = text.trimmed();
        if (text.isEmpty()) return QString::fromUtf8("•");
        const QChar ch = text.front();
        static const QString good = QString::fromUtf8("•●○■▪◆◇✓✔➤➔➢");
        if (good.contains(ch)) return {ch};
        const QString f = font.trimmed().toLower();
        if (f.startsWith("wingdings")) {
            if (ch == QChar(0xf0fc)) return QString::fromUtf8("✓");
            if (ch == QChar(0xf0d8)) return QString::fromUtf8("➤");
            if (ch == QChar(0xf0a7)) return QString::fromUtf8("▪");
            if (ch == 'n') return QString::fromUtf8("■");
            if (ch == 'u') return QString::fromUtf8("◆");
            if (ch == 'v') return QString::fromUtf8("◇");
        }
        if (f == "courier new" && ch == 'o') return QString::fromUtf8("○");
        return QString::fromUtf8("•");
    }

    struct LevelDef {
        QString format;
        QString text;
        QString font;
    };

    class NumberingContext {
    public:
        static NumberingContext load(zip_t *zip) {
            NumberingContext ctx;
            if (hasEntry(zip, "word/numbering.xml"))
                ctx.loadNumbering(parseXml(readEntry(zip, "word/numbering.xml")).get());
            if (hasEntry(zip, "word/styles.xml"))
                ctx.loadStyles(parseXml(readEntry(zip, "word/styles.xml")).get());
            return ctx;
        }

        void resetCountersForPart() { counters_.clear(); }

        [[nodiscard]] std::optional<std::pair<int, int> > resolve(
            std::optional<int> numId, const std::optional<int> ilvl, const QString &style) const {
            if (numId) return std::pair{*numId, ilvl.value_or(0)};
            if (const auto it = styles_.constFind(style); !style.isEmpty() && it != styles_.cend())
                return *it;
            return std::nullopt;
        }

        QString nextPrefix(const int numId, int ilvl) {
            ilvl = std::clamp(ilvl, 0, 8);
            const auto numIt = numToAbstract_.constFind(numId);
            if (numIt == numToAbstract_.cend()) return {};
            const auto absIt = levels_.constFind(*numIt);
            if (absIt == levels_.cend()) return {};
            const auto levelIt = absIt->constFind(ilvl);
            if (levelIt == absIt->cend()) return {};
            const auto &[format, text, font] = *levelIt;
            auto &counts = counters_[numId];
            ++counts[static_cast<size_t>(ilvl)];
            for (int d = ilvl + 1; d < 9; ++d) counts[static_cast<size_t>(d)] = 0;

            if (format.trimmed().compare("bullet", Qt::CaseInsensitive) == 0)
                return bulletGlyph(text, font) + ' ';

            const QString pattern = text.isEmpty() ? "%1." : text;
            QString prefix;
            for (qsizetype i = 0; i < pattern.size(); ++i) {
                if (pattern[i] == '%' && i + 1 < pattern.size() &&
                    pattern[i + 1] >= '1' && pattern[i + 1] <= '9') {
                    const int k = pattern[++i].unicode() - '1';
                    auto ref = absIt->constFind(k);
                    prefix += formatCounter(counts[static_cast<size_t>(k)],
                                            ref == absIt->cend() ? "decimal" : ref->format);
                } else {
                    prefix += pattern[i];
                }
            }
            prefix.replace('\t', ' ');
            prefix.replace(QChar(0xa0), ' ');
            if (!prefix.isEmpty() && !prefix.back().isSpace()) prefix += ' ';
            return prefix;
        }

    private:
        QHash<int, int> numToAbstract_;
        QHash<int, QHash<int, LevelDef> > levels_;
        QHash<QString, std::pair<int, int> > styles_;
        QHash<int, std::array<int, 9> > counters_;

        void loadNumbering(const xmlDoc *doc) {
            std::optional<int> currentAbs, currentLvl, currentNum;
            walk(xmlDocGetRootElement(doc), [&](const xmlNode *node, const bool start) {
                if (!inNs(node, W_NS)) return;
                const auto n = reinterpret_cast<const char *>(node->name);
                if (!start) {
                    if (strcmp(n, "num") == 0) currentNum.reset();
                    else if (strcmp(n, "abstractNum") == 0) {
                        currentAbs.reset();
                        currentLvl.reset();
                    } else if (strcmp(n, "lvl") == 0) currentLvl.reset();
                    return;
                }
                auto intAttr = [&](const char *key) -> std::optional<int> {
                    bool ok = false;
                    const int v = attribute(node, W_NS, key).toInt(&ok);
                    return ok ? std::optional<int>(v) : std::nullopt;
                };
                if (strcmp(n, "num") == 0) currentNum = intAttr("numId");
                else if (strcmp(n, "abstractNumId") == 0 && currentNum) {
                    if (const auto id = intAttr("val")) numToAbstract_[*currentNum] = *id;
                } else if (strcmp(n, "abstractNum") == 0) {
                    currentAbs = intAttr("abstractNumId");
                    if (currentAbs) levels_[*currentAbs];
                } else if (strcmp(n, "lvl") == 0 && currentAbs) {
                    currentLvl = intAttr("ilvl");
                    if (currentLvl) levels_[*currentAbs][*currentLvl];
                } else if (currentAbs && currentLvl) {
                    auto &[format, text, font] = levels_[*currentAbs][*currentLvl];
                    if (strcmp(n, "numFmt") == 0) format = attribute(node, W_NS, "val");
                    else if (strcmp(n, "lvlText") == 0) text = attribute(node, W_NS, "val");
                    else if (strcmp(n, "rFonts") == 0) {
                        font = attribute(node, W_NS, "ascii");
                        if (font.isEmpty()) font = attribute(node, W_NS, "hAnsi");
                    }
                }
            });
        }

        void loadStyles(const xmlDoc *doc) {
            QString styleId;
            std::optional<int> numId, ilvl;
            walk(xmlDocGetRootElement(doc), [&](const xmlNode *node, const bool start) {
                if (!inNs(node, W_NS)) return;
                if (named(node, W_NS, "style")) {
                    if (start) {
                        styleId = attribute(node, W_NS, "styleId");
                        numId.reset();
                        ilvl.reset();
                    } else {
                        if (!styleId.isEmpty() && numId && ilvl)
                            styles_.insert(styleId, {*numId, *ilvl});
                        styleId.clear();
                    }
                    return;
                }
                if (!start || styleId.isEmpty()) return;
                bool ok = false;
                if (named(node, W_NS, "numId")) {
                    const int v = attribute(node, W_NS, "val").toInt(&ok);
                    if (ok) numId = v;
                } else if (named(node, W_NS, "ilvl")) {
                    const int v = attribute(node, W_NS, "val").toInt(&ok);
                    if (ok) ilvl = v;
                }
            });
        }
    };

    bool skipNote(const xmlNode *node) {
        if (const QString type = attribute(node, W_NS, "type").toLower();
            type == "separator" || type == "continuationseparator")
            return true;
        bool ok = false;
        const int id = attribute(node, W_NS, "id").toInt(&ok);
        return ok && id <= 0;
    }

    QString extractWord(const xmlDoc *doc, NumberingContext *ctx) {
        QString out;
        QStringList rowCells;
        QString cell;
        bool inTable = false, inRow = false, inCell = false;
        bool inParagraph = false, prefixEmitted = false;
        std::optional<int> paraNum, paraLevel;
        QString paraStyle;
        bool inNote = false, skipThisNote = false;

        auto target = [&]() -> QString & { return inCell ? cell : out; };
        auto emitPrefix = [&]() {
            if (!inParagraph || prefixEmitted || !ctx) return;
            const auto num = ctx->resolve(paraNum, paraLevel, paraStyle);
            if (!num) return;
            if (const QString prefix = ctx->nextPrefix(num->first, num->second); !prefix.isEmpty()) {
                target() += prefix;
                prefixEmitted = true;
            }
        };
        walk(xmlDocGetRootElement(doc), [&](const xmlNode *node, const bool start) {
            if (!inNs(node, W_NS)) return;
            if (start) {
                if (named(node, W_NS, "footnote") || named(node, W_NS, "endnote")) {
                    inNote = true;
                    skipThisNote = skipNote(node);
                } else if (named(node, W_NS, "tbl")) inTable = true;
                else if (named(node, W_NS, "tr") && inTable) {
                    inRow = true;
                    rowCells.clear();
                } else if (named(node, W_NS, "tc") && inRow) {
                    inCell = true;
                    cell.clear();
                } else if (named(node, W_NS, "p")) {
                    inParagraph = true;
                    prefixEmitted = false;
                    paraNum.reset();
                    paraLevel.reset();
                    paraStyle.clear();
                } else if (named(node, W_NS, "pStyle") && inParagraph)
                    paraStyle = attribute(node, W_NS, "val");
                else if (named(node, W_NS, "numId") && inParagraph) {
                    bool ok = false;
                    int n = attribute(node, W_NS, "val").toInt(&ok);
                    if (ok) paraNum = n;
                } else if (named(node, W_NS, "ilvl") && inParagraph) {
                    bool ok = false;
                    int n = attribute(node, W_NS, "val").toInt(&ok);
                    if (ok) paraLevel = n;
                } else if (!skipThisNote || !inNote) {
                    if (named(node, W_NS, "tab")) {
                        emitPrefix();
                        target() += '\t';
                    } else if (named(node, W_NS, "br") || named(node, W_NS, "cr")) {
                        emitPrefix();
                        target() += '\n';
                    }
                }
            } else {
                if (named(node, W_NS, "t") && (!skipThisNote || !inNote)) {
                    // xmlNodeGetContent resolves character references and inline text.
                    if (xmlChar *value = xmlNodeGetContent(node)) {
                        const QString text = QString::fromUtf8(reinterpret_cast<const char *>(value));
                        xmlFree(value);
                        if (!text.isEmpty()) {
                            emitPrefix();
                            target() += text;
                        }
                    }
                } else if (named(node, W_NS, "p")) {
                    if (!skipThisNote || !inNote) target() += '\n';
                    inParagraph = false;
                } else if (named(node, W_NS, "tc")) {
                    if (inCell) {
                        trimTrailingNewlines(cell);
                        rowCells.append(cell);
                        inCell = false;
                    }
                } else if (named(node, W_NS, "tr")) {
                    if (inRow) {
                        out += rowCells.join('\t');
                        out += '\n';
                        inRow = false;
                    }
                } else if (named(node, W_NS, "tbl")) {
                    if (inTable) {
                        if (!out.isEmpty() && !out.endsWith('\n')) out += '\n';
                        inTable = false;
                    }
                } else if (named(node, W_NS, "footnote") || named(node, W_NS, "endnote")) {
                    inNote = false;
                    skipThisNote = false;
                }
            }
        });
        return out;
    }

    QString extractOdf(const xmlDoc *doc) {
        QString out, cell;
        QStringList rowCells;
        int listLevel = 0;
        bool inTable = false, inRow = false, inCell = false;
        bool inParagraph = false, prefixEmitted = false;

        auto target = [&]() -> QString & { return inCell ? cell : out; };
        auto emitPrefix = [&]() {
            if (!inParagraph || prefixEmitted) return;
            if (listLevel > 0)
                target() += QString((listLevel - 1) * 2, ' ') + "- ";
            prefixEmitted = true;
        };
        auto append = [&](const QString &text) {
            if (!text.isEmpty()) {
                emitPrefix();
                target() += text;
            }
        };
        std::function<void(xmlNode *)> visit = [&](const xmlNode *node) {
            for (; node; node = node->next) {
                if (node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE) {
                    append(textOf(node));
                    continue;
                }
                if (node->type != XML_ELEMENT_NODE) {
                    if (node->children) visit(node->children);
                    continue;
                }
                if (named(node, TEXT_NS, "list")) ++listLevel;
                else if (named(node, TEXT_NS, "p") || named(node, TEXT_NS, "h")) {
                    inParagraph = true;
                    prefixEmitted = false;
                    emitPrefix();
                } else if (named(node, TEXT_NS, "tab")) {
                    emitPrefix();
                    target() += '\t';
                } else if (named(node, TEXT_NS, "line-break")) {
                    emitPrefix();
                    target() += '\n';
                } else if (named(node, TEXT_NS, "s")) {
                    emitPrefix();
                    bool ok = false;
                    const int n = attribute(node, TEXT_NS, "c").toInt(&ok);
                    target() += QString(ok && n > 0 ? n : 1, ' ');
                }

                if (named(node, TABLE_NS, "table")) inTable = true;
                else if (named(node, TABLE_NS, "table-row") && inTable) {
                    inRow = true;
                    rowCells.clear();
                } else if (named(node, TABLE_NS, "table-cell") && inRow) {
                    inCell = true;
                    cell.clear();
                }

                if (node->children) visit(node->children);

                if (named(node, TEXT_NS, "list")) listLevel = std::max(0, listLevel - 1);
                else if (named(node, TEXT_NS, "p") || named(node, TEXT_NS, "h")) {
                    target() += '\n';
                    inParagraph = false;
                }

                if (named(node, TABLE_NS, "table-cell") && inCell) {
                    trimTrailingNewlines(cell);
                    rowCells.append(cell);
                    inCell = false;
                } else if (named(node, TABLE_NS, "table-row") && inRow) {
                    out += rowCells.join('\t') + '\n';
                    inRow = false;
                } else if (named(node, TABLE_NS, "table") && inTable) {
                    if (!out.isEmpty() && !out.endsWith('\n')) out += '\n';
                    inTable = false;
                }
            }
        };
        visit(xmlDocGetRootElement(doc));
        return out;
    }
} // namespace

bool OpenXmlHelper::isDocx(const QString &path) {
    if (!QFileInfo(path).isFile() || !path.endsWith(".docx", Qt::CaseInsensitive))
        return false;
    try {
        const auto zip = openZip(path);
        return hasEntry(zip.get(), "word/document.xml") &&
               hasEntry(zip.get(), "[Content_Types].xml");
    } catch (const std::exception &) { return false; }
}

bool OpenXmlHelper::isOdt(const QString &path) {
    if (!QFileInfo(path).isFile() || !path.endsWith(".odt", Qt::CaseInsensitive))
        return false;
    try {
        const auto zip = openZip(path);
        if (!hasEntry(zip.get(), "content.xml")) return false;
        if (!hasEntry(zip.get(), "mimetype")) return true;
        return QString::fromLatin1(readEntry(zip.get(), "mimetype")).trimmed() ==
               "application/vnd.oasis.opendocument.text";
    } catch (const std::exception &) { return false; }
}

QString OpenXmlHelper::extractDocxAllText(const QString &path,
                                          const bool includePartHeadings,
                                          const bool normalizeNewlines,
                                          const bool includeNumbering) {
    const auto zip = openZip(path);
    std::optional<NumberingContext> numbering;
    if (includeNumbering) numbering = NumberingContext::load(zip.get());

    QStringList parts{
        "word/document.xml", "word/footnotes.xml",
        "word/endnotes.xml", "word/comments.xml"
    };
    QStringList headers, footers;
    const zip_int64_t count = zip_get_num_entries(zip.get(), 0);
    for (zip_int64_t i = 0; i < count; ++i) {
        const char *raw = zip_get_name(zip.get(), static_cast<zip_uint64_t>(i), 0);
        if (!raw) continue;
        if (const QString name = QString::fromUtf8(raw); name.startsWith("word/header", Qt::CaseInsensitive) &&
                                                         name.endsWith(".xml", Qt::CaseInsensitive))
            headers.append(name);
        else if (name.startsWith("word/footer", Qt::CaseInsensitive) &&
                 name.endsWith(".xml", Qt::CaseInsensitive))
            footers.append(name);
    }
    auto less = [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    };
    std::ranges::sort(headers, less);
    std::ranges::sort(footers, less);
    parts += headers;
    parts += footers;

    QSet<QString> seen;
    QString out;
    bool headerFooterHeadingEmitted = false;
    for (const QString &part: std::as_const(parts)) {
        const QString key = part.toLower();
        if (seen.contains(key)) continue;
        seen.insert(key);
        const QByteArray entry = part.toUtf8();
        if (!hasEntry(zip.get(), entry)) continue;
        // Separate headers/footers from the main document content.
        if (!headerFooterHeadingEmitted &&
            (part.startsWith("word/header", Qt::CaseInsensitive) ||
             part.startsWith("word/footer", Qt::CaseInsensitive))) {
            if (!out.isEmpty() && !out.endsWith('\n'))
                out += '\n';

            out += "\n=== Header / Footer ===\n";
            headerFooterHeadingEmitted = true;
        }
        if (includePartHeadings) {
            if (!out.isEmpty() && !out.endsWith('\n') && !out.endsWith('\r')) out += '\n';
            out += "=== " + part + " ===\n";
        }
        if (numbering) numbering->resetCountersForPart();
        auto doc = parseXml(readEntry(zip.get(), entry));
        out += extractWord(doc.get(), numbering ? &*numbering : nullptr);
        if (!out.isEmpty() && !out.endsWith('\n') && !out.endsWith('\r')) out += '\n';
    }
    if (normalizeNewlines) ::normalizeNewlines(out);
    return out;
}

QString OpenXmlHelper::extractOdtAllText(const QString &path, const bool normalizeNewlines) {
    const auto zip = openZip(path);
    if (!hasEntry(zip.get(), "content.xml"))
        throw std::runtime_error("content.xml not found. Not a valid ODT?");
    const auto doc = parseXml(readEntry(zip.get(), "content.xml"));
    QString out = extractOdf(doc.get());
    if (normalizeNewlines) ::normalizeNewlines(out);
    return out;
}
