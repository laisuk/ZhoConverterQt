#pragma once

#include "OpenccFmmsegHelper.hpp"
#include <zip.h>

#include <fstream>
#include <regex>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_set>
#include <cctype>
#include <iterator>
#include <utility>

class OfficeConverter {
public:
    static inline const std::unordered_set<std::string> OFFICE_EXTENSIONS = {
        "docx", "xlsx", "pptx", "odt", "ods", "odp", "epub"
    };

    struct Result {
        bool success;
        std::string message;
    };

    struct BytesResult {
        bool success;
        std::string message;
        std::vector<uint8_t> outputBytes;
    };

    static Result Convert(const std::string &inputPath,
                          const std::string &outputPath,
                          const std::string &format,
                          OpenccFmmsegHelper &helper,
                          const opencc_config_t &config,
                          bool punctuation,
                          bool keepFont = false) {
        std::ifstream in(inputPath, std::ios::binary);
        if (!in) return {false, "❌ Cannot open input file."};

        std::vector<uint8_t> inputBytes{
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()
        };

        auto [success, message, outputBytes] =
                ConvertBytes(inputBytes, format, helper, config, punctuation, keepFont);
        if (!success) return {false, message};

        std::ofstream out(outputPath, std::ios::binary);
        if (!out) return {false, "❌ Cannot open output file for writing."};

        out.write(reinterpret_cast<const char *>(outputBytes.data()),
                  static_cast<std::streamsize>(outputBytes.size()));

        return {true, message};
    }

    static BytesResult ConvertBytes(const std::vector<uint8_t> &inputZipBytes,
                                    const std::string &format,
                                    OpenccFmmsegHelper &helper,
                                    const opencc_config_t &config,
                                    bool punctuation,
                                    bool keepFont = false) {
        if (inputZipBytes.empty())
            return {false, "❌ Input ZIP buffer is empty.", {}};

        const std::string normalizedFormat = toLowerCopy(format);

        // ---- Open input ZIP from memory ----
        zip_error_t zipError{};
        zip_error_init(&zipError);

        zip_source_t *inSrc = zip_source_buffer_create(
            inputZipBytes.data(),
            inputZipBytes.size(),
            0,
            &zipError
        );
        if (!inSrc) {
            zip_error_fini(&zipError);
            return {false, "❌ Failed to create ZIP source from memory.", {}};
        }

        zip_t *zin = zip_open_from_source(inSrc, 0, &zipError);
        if (!zin) {
            zip_source_free(inSrc);
            zip_error_fini(&zipError);
            return {false, "❌ Failed to open ZIP archive from memory.", {}};
        }

        const zip_int64_t n = zip_get_num_entries(zin, 0);
        std::vector<Entry> entries;
        entries.reserve(static_cast<size_t>(std::max<zip_int64_t>(0, n)));

        for (zip_uint64_t i = 0; i < static_cast<zip_uint64_t>(n); ++i) {
            const char *nm = zip_get_name(zin, i, 0);
            if (!nm) continue;

            Entry e;
            e.name = nm;
            if (!IsSafeZipEntryName(e.name)) continue;
            e.isDir = (!e.name.empty() && e.name.back() == '/');

            if (!e.isDir) {
                zip_stat_t st{};
                zip_stat_init(&st);

                if (zip_stat_index(zin, i, 0, &st) != 0) {
                    entries.push_back(std::move(e));
                    continue;
                }

                zip_file_t *zf = zip_fopen_index(zin, i, 0);
                if (!zf) {
                    entries.push_back(std::move(e));
                    continue;
                }

                e.data.resize(static_cast<size_t>(st.size));

                zip_int64_t off = 0;
                while (off < static_cast<zip_int64_t>(e.data.size())) {
                    const auto remaining =
                            static_cast<zip_uint64_t>(e.data.size() - static_cast<size_t>(off));
                    const zip_int64_t r =
                            zip_fread(zf, e.data.data() + off, remaining);
                    if (r <= 0) break;
                    off += r;
                }

                zip_fclose(zf);

                if (off >= 0 && static_cast<size_t>(off) < e.data.size())
                    e.data.resize(static_cast<size_t>(off));
            }

            entries.push_back(std::move(e));
        }

        zip_close(zin); // closes zin and releases its ownership of inSrc

        const std::vector<size_t> targets = getTargetEntryIndices(normalizedFormat, entries);
        if (targets.empty())
            return {false, "❌ No target fragments found in archive for this format.", {}};

        size_t convertedCount = 0;

        for (const size_t idx: targets) {
            if (idx >= entries.size()) continue;

            auto &[entry, isDir, data] = entries[idx];
            if (isDir) continue;

            std::string text(data.begin(), data.end());

            std::map<std::string, std::string> fontMap;
            if (keepFont && shouldMaskFonts(normalizedFormat, entry))
                maskFont(text, normalizedFormat, fontMap);

            std::string converted;
            if (normalizedFormat == "xlsx") {
                converted = convertXlsxEntry(
                    text,
                    entry,
                    helper,
                    config,
                    punctuation
                );
            } else {
                converted = helper.convert_cfg(text, config, punctuation);
            }

            if (!fontMap.empty()) {
                for (const auto &[marker, original]: fontMap) {
                    size_t pos = 0;
                    while ((pos = converted.find(marker, pos)) != std::string::npos) {
                        converted.replace(pos, marker.length(), original);
                        pos += original.length();
                    }
                }
            }

            data.assign(converted.begin(), converted.end());
            ++convertedCount;
        }

        if (convertedCount == 0)
            return {false, "❌ No fragments were converted. Nothing changed.", {}};

        // ---- Create output ZIP to memory ----
        zip_error_t outErr{};
        zip_error_init(&outErr);

        zip_source_t *outSrc = zip_source_buffer_create(nullptr, 0, 0, &outErr);
        if (!outSrc) {
            zip_error_fini(&outErr);
            return {false, "❌ Failed to create output ZIP buffer source.", {}};
        }

        // Keep an extra reference so we can read bytes after zip_close()/zip_discard().
        zip_source_keep(outSrc);

        zip_t *zOut = zip_open_from_source(outSrc, ZIP_CREATE | ZIP_TRUNCATE, &outErr);
        if (!zOut) {
            zip_source_free(outSrc);
            zip_error_fini(&outErr);
            return {false, "❌ Failed to open output ZIP from buffer source.", {}};
        }

        auto failOutput = [&](const std::string &msg) -> BytesResult {
            zip_discard(zOut); // releases zip_t's ownership of outSrc
            zip_source_free(outSrc); // release our kept ref
            zip_error_fini(&outErr);
            return {false, msg, {}};
        };

        auto addDir = [&](const std::string &entryName) -> bool {
            std::string nm = entryName;
            if (nm.empty()) return true;
            if (nm.back() != '/') nm.push_back('/');

            const zip_int64_t r = zip_dir_add(zOut, nm.c_str(), ZIP_FL_ENC_UTF_8);
            return r >= 0;
        };

        auto addFile = [&](const std::string &entryName,
                           const std::vector<uint8_t> &data,
                           const bool storeNoCompress) -> bool {
            zip_source_t *src = zip_source_buffer(zOut, data.data(), data.size(), 0);
            if (!src) return false;

            const zip_int64_t fileIndex = zip_file_add(
                zOut,
                entryName.c_str(),
                src,
                ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE
            );

            if (fileIndex < 0) {
                zip_source_free(src);
                return false;
            }

            if (storeNoCompress) {
                zip_set_file_compression(
                    zOut,
                    static_cast<zip_uint64_t>(fileIndex),
                    ZIP_CM_STORE,
                    0
                );
            }
            return true;
        };

        auto isMimeName = [](const std::string &nm) -> bool {
            return nm == "mimetype" || nm == "./mimetype" || nm == "/mimetype";
        };

        bool addedMime = false;
        if (normalizedFormat == "epub") {
            for (const auto &[name, isDir, data]: entries) {
                if (isDir) continue;
                if (!isMimeName(name)) continue;

                if (!addFile("mimetype", data, true))
                    return failOutput("❌ Failed to add EPUB mimetype entry.");

                addedMime = true;
                break;
            }
        }

        for (const auto &[name, isDir, data]: entries) {
            if (addedMime && isMimeName(name))
                continue;

            if (isDir) {
                if (!addDir(name))
                    return failOutput("❌ Failed to add directory entry: " + name);
                continue;
            }

            if (!addFile(name, data, false))
                return failOutput("❌ Failed to add file to output ZIP: " + name);
        }

        if (zip_close(zOut) != 0) {
            zip_source_free(outSrc);
            zip_error_fini(&outErr);
            return {false, "❌ Failed to finalize output ZIP.", {}};
        }

        std::vector<uint8_t> outBytes;
        if (zip_source_open(outSrc) == 0) {
            uint8_t tmp[8192];
            while (true) {
                const zip_int64_t r = zip_source_read(outSrc, tmp, sizeof(tmp));
                if (r < 0) break;
                if (r == 0) break;
                outBytes.insert(outBytes.end(), tmp, tmp + r);
            }
            zip_source_close(outSrc);
        }

        zip_source_free(outSrc);
        zip_error_fini(&outErr);

        if (outBytes.empty())
            return {false, "❌ Output ZIP buffer is empty (unexpected).", {}};

        return {
            true,
            "✅ Conversion completed.\n✅ Converted " +
            std::to_string(convertedCount) + " fragment(s).\n",
            std::move(outBytes)
        };
    }

private:
    struct Entry {
        std::string name;
        bool isDir = false;
        std::vector<uint8_t> data;
    };

    static inline bool IsSafeZipEntryName(const std::string &name) {
        if (name.empty()) return false;
        if (name[0] == '/' || name[0] == '\\') return false;
        if (name.find('\0') != std::string::npos) return false;
        if (name.find('\\') != std::string::npos) return false;
        if (name.find("..") != std::string::npos) {
            if (name.find("../") != std::string::npos || name.find("/..") != std::string::npos)
                return false;
        }
        return true;
    }

    static inline std::string toLowerCopy(std::string s) {
        std::transform(
            s.begin(),
            s.end(),
            s.begin(),
            [](const unsigned char c) { return static_cast<char>(std::tolower(c)); }
        );
        return s;
    }

    static inline bool startsWith(const std::string &s, const std::string &prefix) {
        return s.size() >= prefix.size() &&
               s.compare(0, prefix.size(), prefix) == 0;
    }

    static inline bool endsWith(const std::string &s, const std::string &suf) {
        return s.size() >= suf.size() &&
               s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
    }

    static inline bool isXlsxSharedStrings(const std::string &entryName) {
        return entryName == "xl/sharedStrings.xml";
    }

    static inline bool isXlsxWorksheet(const std::string &entryName) {
        return startsWith(entryName, "xl/worksheets/") && endsWith(entryName, ".xml");
    }

    static inline bool shouldMaskFonts(const std::string &format,
                                       const std::string &entryName) {
        if (format != "xlsx")
            return true;

        return isXlsxSharedStrings(entryName);
    }

    static std::vector<size_t> getTargetEntryIndices(const std::string &format,
                                                     const std::vector<Entry> &entries) {
        std::vector<size_t> result;
        result.reserve(32);

        if (format == "docx") {
            for (size_t i = 0; i < entries.size(); ++i) {
                if (!entries[i].isDir && entries[i].name == "word/document.xml")
                    result.push_back(i);
            }
        } else if (format == "xlsx") {
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].isDir) continue;

                if (const std::string &nm = entries[i].name; isXlsxSharedStrings(nm) || isXlsxWorksheet(nm))
                    result.push_back(i);
            }
        } else if (format == "pptx") {
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].isDir) continue;
                const std::string &nm = entries[i].name;
                if (nm.rfind("ppt/", 0) != 0) continue;
                if (!endsWith(nm, ".xml")) continue;

                if (nm.find("slides/slide") != std::string::npos ||
                    nm.find("notesSlides/notesSlide") != std::string::npos) {
                    result.push_back(i);
                }
            }
        } else if (format == "odt" || format == "ods" || format == "odp") {
            for (size_t i = 0; i < entries.size(); ++i) {
                if (!entries[i].isDir && entries[i].name == "content.xml")
                    result.push_back(i);
            }
        } else if (format == "epub") {
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].isDir) continue;

                if (const std::string lower = toLowerCopy(entries[i].name);
                    endsWith(lower, ".xhtml") || endsWith(lower, ".html") ||
                    endsWith(lower, ".opf") || endsWith(lower, ".ncx")) {
                    result.push_back(i);
                }
            }
        }

        return result;
    }

    static std::string convertXlsxEntry(const std::string &xml,
                                        const std::string &entryName,
                                        const OpenccFmmsegHelper &helper,
                                        const opencc_config_t &config,
                                        const bool punctuation) {
        if (isXlsxSharedStrings(entryName))
            return helper.convert_cfg(xml, config, punctuation);

        if (isXlsxWorksheet(entryName))
            return convertXlsxInlineStrings(xml, helper, config, punctuation);

        return xml;
    }

    static std::string convertXlsxInlineStrings(const std::string &xml,
                                                const OpenccFmmsegHelper &helper,
                                                const opencc_config_t &config,
                                                const bool punctuation) {
        static const std::regex cellPattern(
            R"(<c\b[^>]*>[\s\S]*?<\/c>)",
            std::regex::ECMAScript
        );

        std::string out;
        out.reserve(xml.size());

        std::sregex_iterator it(xml.begin(), xml.end(), cellPattern);

        auto last = xml.cbegin();

        for (const std::sregex_iterator end; it != end; ++it) {
            const std::smatch &m = *it;
            const std::string cellXml = m.str();

            out.append(last, m.prefix().second);

            if (const auto tagEnd = cellXml.find('>'); tagEnd == std::string::npos) {
                out.append(cellXml);
            } else {
                const std::string openTag = cellXml.substr(0, tagEnd);
                const bool isInlineStr =
                        openTag.find(R"(t="inlineStr")") != std::string::npos ||
                        openTag.find(R"(t='inlineStr')") != std::string::npos;

                if (isInlineStr) {
                    out.append(convertXlsxInlineStringCell(cellXml, helper, config, punctuation));
                } else {
                    out.append(cellXml);
                }
            }

            last = m.suffix().first;
        }

        out.append(last, xml.cend());
        return out;
    }

    static std::string convertXlsxInlineStringCell(const std::string &cellXml,
                                                   const OpenccFmmsegHelper &helper,
                                                   const opencc_config_t &config,
                                                   const bool punctuation) {
        static const std::regex textNodePattern(
            R"((<t\b[^>]*>)([\s\S]*?)(<\/t>))",
            std::regex::ECMAScript
        );

        std::string out;
        out.reserve(cellXml.size());

        std::sregex_iterator it(cellXml.begin(), cellXml.end(), textNodePattern);

        auto last = cellXml.cbegin();

        for (const std::sregex_iterator end; it != end; ++it) {
            const std::smatch &m = *it;

            out.append(last, m.prefix().second);

            const std::string openTag = m[1].str();
            const std::string innerText = m[2].str();
            const std::string closeTag = m[3].str();

            if (innerText.empty()) {
                out.append(m.str());
            } else {
                const std::string convertedText =
                        helper.convert_cfg(innerText, config, punctuation);
                out.append(openTag);
                out.append(convertedText);
                out.append(closeTag);
            }

            last = m.suffix().first;
        }

        out.append(last, cellXml.cend());
        return out;
    }

    static void maskFont(std::string &xml,
                         const std::string &format,
                         std::map<std::string, std::string> &fontMap) {
        std::regex pattern;
        if (format == "docx")
            pattern = std::regex(R"((w:(?:eastAsia|ascii|hAnsi|cs)=\")(.*?)(\"))");
        else if (format == "xlsx")
            pattern = std::regex(R"((val=\")(.*?)(\"))");
        else if (format == "pptx")
            pattern = std::regex(R"((typeface=\")(.*?)(\"))");
        else if (format == "odt" || format == "ods" || format == "odp")
            pattern = std::regex(
                R"(((?:style:font-name(?:-asian|-complex)?|svg:font-family|style:name)=[\'\"])([^\'\"]+)([\'\"]))");
        else if (format == "epub")
            pattern = std::regex(R"((font-family\s*:\s*)([^;\"']+)([;\"']?))");
        else
            return;

        std::smatch match;
        size_t counter = 0;
        std::string result;
        result.reserve(xml.size());

        auto begin = xml.cbegin();
        const auto end = xml.cend();

        while (std::regex_search(begin, end, match, pattern)) {
            const std::string marker = "__F_O_N_T_" + std::to_string(counter++) + "__";
            fontMap[marker] = match[2].str();

            result.append(match.prefix().first, match.prefix().second);
            result.append(match[1].first, match[1].second);
            result.append(marker);

            if (match.size() > 3)
                result.append(match[3].first, match[3].second);

            begin = match.suffix().first;
        }

        result.append(begin, end);
        xml = std::move(result);
    }
};
