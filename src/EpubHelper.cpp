#include "EpubHelper.h"

#include <QByteArray>
#include <QFileInfo>
#include <QFile>
#include <functional>
#include <QHash>
#include <QVector>
#include <QRegularExpression>

#include <zip.h>
#include <libxml/parser.h>
#include <libxml/HTMLparser.h>

#include <memory>
#include <stdexcept>
#include <limits>

namespace {
    struct ZipCloser {
        void operator()(zip_t *z) const { if (z) zip_close(z); }
    };

    struct XmlCloser {
        void operator()(xmlDoc *d) const { if (d) xmlFreeDoc(d); }
    };

    using ZipPtr = std::unique_ptr<zip_t, ZipCloser>;
    using DocPtr = std::unique_ptr<xmlDoc, XmlCloser>;

    ZipPtr openArchive(const QString &path) {
        const QByteArray encoded = QFile::encodeName(path);
        int error = 0;
        ZipPtr archive(zip_open(encoded.constData(), ZIP_RDONLY, &error));
        if (!archive) throw std::runtime_error("Cannot open EPUB archive");
        return archive;
    }

    bool hasEntry(zip_t *archive, const QByteArray &name) {
        return zip_name_locate(archive, name.constData(), 0) >= 0;
    }

    QByteArray readEntry(zip_t *archive, const QByteArray &name) {
        const zip_int64_t index = zip_name_locate(archive, name.constData(), 0);
        if (index < 0) throw std::runtime_error("EPUB entry not found");
        zip_stat_t stat;
        zip_stat_init(&stat);
        if (zip_stat_index(archive, static_cast<zip_uint64_t>(index), 0, &stat) != 0)
            throw std::runtime_error("Cannot stat EPUB entry");
        if (!(stat.valid & ZIP_STAT_SIZE) || stat.size > static_cast<zip_uint64_t>(std::numeric_limits<
                qsizetype>::max()))
            throw std::runtime_error("EPUB entry is too large");
        const std::unique_ptr<zip_file_t, decltype(&zip_fclose)> file(
            zip_fopen_index(archive, static_cast<zip_uint64_t>(index), 0), &zip_fclose);
        if (!file) throw std::runtime_error("Cannot read EPUB entry");
        QByteArray data;
        constexpr zip_uint64_t chunk = 64 * 1024;
        char buffer[chunk];
        while (true) {
            const zip_int64_t n = zip_fread(file.get(), buffer, chunk);
            if (n < 0) throw std::runtime_error("Cannot decompress EPUB entry");
            if (n == 0) break;
            data.append(buffer, static_cast<qsizetype>(n));
        }
        return data;
    }

    QString localName(const xmlNode *node) {
        return QString::fromUtf8(reinterpret_cast<const char *>(node->name)).toLower();
    }

    QString attribute(const xmlNode *node, const char *key) {
        xmlChar *value = xmlGetProp(
            node, reinterpret_cast<const xmlChar *>(key));

        const QString result = value
                                   ? QString::fromUtf8(reinterpret_cast<const char *>(value))
                                   : QString{};

        if (value)
            xmlFree(value);

        return result;
    }

    void visit(xmlNode *node, const std::function<void(xmlNode *)> &fn) {
        for (; node; node = node->next) {
            if (node->type == XML_ELEMENT_NODE) fn(node);
            visit(node->children, fn);
        }
    }

    DocPtr parseXml(const QByteArray &data) {
        if (data.size() > std::numeric_limits<int>::max()) throw std::runtime_error("XML entry too large");
        return DocPtr(xmlReadMemory(data.constData(), static_cast<int>(data.size()), nullptr, nullptr,
                                    XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING));
    }

    QString opfPath(zip_t *archive) {
        if (!hasEntry(archive, "META-INF/container.xml")) return {};
        const auto doc = parseXml(readEntry(archive, "META-INF/container.xml"));
        if (!doc) throw std::runtime_error("Invalid EPUB container.xml");
        QString result;
        visit(xmlDocGetRootElement(doc.get()), [&](const xmlNode *node) {
            if (!result.isEmpty() || localName(node) != "rootfile") return;
            result = attribute(node, "full-path").trimmed();
            if (result.isEmpty()) result = attribute(node, "fullpath").trimmed();
        });
        return result;
    }

    QString combinePath(const QString &directory, QString href) {
        href.replace('\\', '/');
        QStringList parts;
        for (const QString &part: (directory + href).split('/', Qt::SkipEmptyParts)) {
            if (part == ".") continue;
            if (part == "..") { if (!parts.isEmpty()) parts.removeLast(); } else parts.append(part);
        }
        return parts.join('/');
    }

    bool looksHtml(const QString &media, const QString &href) {
        const QString mt = media.trimmed().toLower();
        const QString h = href.toLower();
        return mt.contains("html") || h.endsWith(".xhtml") || h.endsWith(".html") || h.endsWith(".htm");
    }

    void paragraphBreak(QString &out) {
        while (out.endsWith(' ') || out.endsWith('\t')) out.chop(1);
        if (out.isEmpty() || out.endsWith("\n\n")) return;
        if (!out.endsWith('\n')) out += '\n';
        out += '\n';
    }

    void appendText(QString &out, const QString &value) {
        for (const QChar ch: value) {
            if (ch.isSpace()) {
                if (!out.isEmpty() && !out.back().isSpace()) out += ' ';
            } else out += ch;
        }
    }

    void walkHtml(const xmlNode *node, QString &out, const bool skip = false) {
        static const QStringList excluded = {"script", "style", "head", "svg", "math", "noscript"};
        static const QStringList blocks = {
            "p", "div", "section", "article", "blockquote", "li",
            "h1", "h2", "h3", "h4", "h5", "h6", "hr"
        };
        for (; node; node = node->next) {
            if (node->type == XML_TEXT_NODE || node->type == XML_CDATA_SECTION_NODE) {
                if (!skip && node->content)
                    appendText(
                        out, QString::fromUtf8(reinterpret_cast<const char *>(node->content)));
                continue;
            }
            if (node->type != XML_ELEMENT_NODE) continue;
            const QString name = localName(node);
            if (skip || excluded.contains(name)) continue;
            const bool block = blocks.contains(name);
            if (block) paragraphBreak(out);
            if (name == "br") out += '\n';
            walkHtml(node->children, out);
            if (block) paragraphBreak(out);
        }
    }

    QString extractHtml(const QByteArray &bytes) {
        if (bytes.size() > std::numeric_limits<int>::max()) throw std::runtime_error("HTML entry too large");
        const DocPtr doc(htmlReadMemory(bytes.constData(), static_cast<int>(bytes.size()), nullptr, "UTF-8",
                                        HTML_PARSE_RECOVER | HTML_PARSE_NONET | HTML_PARSE_NOERROR |
                                        HTML_PARSE_NOWARNING));
        if (!doc) throw std::runtime_error("Cannot parse EPUB chapter");
        QString text;
        walkHtml(xmlDocGetRootElement(doc.get()), text);
        text.remove(QChar(0x00ad));
        text.replace(QChar(0x00a0), QChar(' '));
        return text;
    }

    struct ManifestItem {
        QString href;
        QString media;
        bool nav = false;
    };
}

bool EpubHelper::isEpub(const QString &path) {
    if (const QFileInfo info(path); !info.isFile() || info.suffix().compare("epub", Qt::CaseInsensitive) != 0) return
            false;
    try {
        const auto archive = openArchive(path);
        return hasEntry(archive.get(), "META-INF/container.xml");
    } catch (...) { return false; }
}

QString EpubHelper::extractEpubAllText(const QString &epubPath, const bool includePartHeadings,
                                       const bool normalizeNewlines, const bool skipNavDocuments) {
    const auto archive = openArchive(epubPath);
    const QString opf = opfPath(archive.get());
    if (opf.isEmpty()) throw std::runtime_error("container.xml has no OPF rootfile");
    if (!hasEntry(archive.get(), opf.toUtf8())) throw std::runtime_error("OPF not found");
    const auto doc = parseXml(readEntry(archive.get(), opf.toUtf8()));
    if (!doc) throw std::runtime_error("Invalid EPUB OPF");
    QHash<QString, ManifestItem> manifest;
    QStringList spine;
    static const QRegularExpression whitespaceRe(QStringLiteral("\\s+"));

    visit(xmlDocGetRootElement(doc.get()), [&](const xmlNode *node) {
        if (const QString name = localName(node); name == "item") {
            const QString id = attribute(node, "id"), href = attribute(node, "href");
            if (id.isEmpty() || href.isEmpty()) return;

            const QStringList props = attribute(node, "properties")
                    .split(whitespaceRe, Qt::SkipEmptyParts);

            bool nav = false;
            for (const QString &p: props)
                if (p.compare("nav", Qt::CaseInsensitive) == 0)
                    nav = true;

            manifest.insert(id, {.href = href, .media = attribute(node, "media-type"), .nav = nav});
        } else if (name == "itemref") {
            if (const QString ref = attribute(node, "idref"); !ref.isEmpty()) spine.append(ref);
        }
    });
    const QString directory = opf.contains('/') ? opf.left(opf.lastIndexOf('/') + 1) : QString{};
    QString result;
    for (const QString &ref: spine) {
        const auto it = manifest.constFind(ref);
        if (it == manifest.cend() || !looksHtml(it->media, it->href) || (skipNavDocuments && it->nav)) continue;
        const QString name = combinePath(directory, it->href);
        if (!hasEntry(archive.get(), name.toUtf8())) continue;
        if (includePartHeadings) {
            if (!result.isEmpty() && !result.endsWith('\n')) result += '\n';
            result += "=== " + name + " ===\n";
        }
        result += extractHtml(readEntry(archive.get(), name.toUtf8()));
        if (!result.endsWith('\n')) result += '\n';
        result += '\n';
    }
    if (normalizeNewlines) {
        result.replace("\r\n", "\n");
        result.replace('\r', '\n');
    }
    static const QRegularExpression excess("\\n{3,}");
    result.replace(excess, "\n\n");
    return result;
}
