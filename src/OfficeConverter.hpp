#pragma once

#include "OpenccFmmsegHelper.hpp"
#include <zip.h>

#include <fstream>
#include <regex>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <random>
#include <unordered_set>
#include <cctype>

class OfficeConverter {
public:
    static inline const std::unordered_set<std::string> OFFICE_EXTENSIONS = {
        "docx", "xlsx", "pptx", "odt", "ods", "odp", "epub"
    };

    struct Result {
        bool success;
        std::string message;
    };

    // In-memory conversion result
    struct BytesResult {
        bool success;
        std::string message;
        std::vector<uint8_t> outputBytes; // valid if success==true
    };

    // ------------------------- Backward compatible File IO API -------------------------
    // Now implemented as: Read file -> ConvertBytes(core) -> Write file
    static Result Convert(const std::string &inputPath,
                          const std::string &outputPath,
                          const std::string &format,
                          OpenccFmmsegHelper &helper,
                          const opencc_config_t &config,
                          bool punctuation,
                          bool keepFont = false) {
        // Read input file as bytes
        std::ifstream in(inputPath, std::ios::binary);
        if (!in) return {false, "❌ Cannot open input file."};

        std::vector<uint8_t> inputBytes{
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()
        };

        // Core conversion
        auto [success, message, outputBytes] = ConvertBytes(inputBytes, format, helper, config, punctuation, keepFont);
        if (!success) return {false, message};

        // Write output file
        std::ofstream out(outputPath, std::ios::binary);
        if (!out) return {false, "❌ Cannot open output file for writing."};

        out.write(reinterpret_cast<const char *>(outputBytes.data()),
                  static_cast<std::streamsize>(outputBytes.size()));

        return {true, message};
    }

    // ------------------------- In-memory ZIP Bytes Core -------------------------
    // Convert an Office/EPUB ZIP blob and return a new ZIP blob.
    static BytesResult ConvertBytes(const std::vector<uint8_t> &inputZipBytes,
                                    const std::string &format,
                                    OpenccFmmsegHelper &helper,
                                    const opencc_config_t &config,
                                    bool punctuation,
                                    bool keepFont = false) {
        if (inputZipBytes.empty())
            return {false, "❌ Input ZIP buffer is empty.", {}};

        // ---- Open input ZIP from memory ----
        zip_error_t zip_error;
        zip_error_init(&zip_error);

        zip_source_t *inSrc = zip_source_buffer_create(
            inputZipBytes.data(),
            inputZipBytes.size(),
            0,
            &zip_error
        );
        if (!inSrc) {
            zip_error_fini(&zip_error);
            return {false, "❌ Failed to create ZIP source from memory.", {}};
        }

        zip_t *zin = zip_open_from_source(inSrc, 0, &zip_error);
        if (!zin) {
            zip_source_free(inSrc);
            zip_error_fini(&zip_error);
            return {false, "❌ Failed to open ZIP archive from memory.", {}};
        }

        // struct Entry
        // {
        //     std::string name;
        //     bool isDir = false;
        //     std::vector<uint8_t> data; // empty for directories
        // };

        // ---- Read all entries ----
        const zip_int64_t n = zip_get_num_entries(zin, 0);
        std::vector<Entry> entries;
        entries.reserve(static_cast<size_t>(std::max<zip_int64_t>(0, n)));

        for (zip_uint64_t i = 0; i < static_cast<zip_uint64_t>(n); ++i) {
            const char *nm = zip_get_name(zin, i, 0);
            if (!nm) continue;

            Entry e;
            e.name = nm;
            if (!IsSafeZipEntryName(e.name)) continue; // or fail hard
            e.isDir = (!e.name.empty() && e.name.back() == '/');

            if (!e.isDir) {
                zip_stat_t st;
                zip_stat_init(&st);
                if (zip_stat_index(zin, i, 0, &st) != 0) {
                    // If stat fails, keep as empty file
                    entries.push_back(std::move(e));
                    continue;
                }

                zip_file_t *zf = zip_fopen_index(zin, i, 0);
                if (!zf) {
                    // Some entries might be unreadable; keep as empty payload
                    entries.push_back(std::move(e));
                    continue;
                }

                e.data.resize(st.size);

                zip_int64_t off = 0;
                while (off < static_cast<zip_int64_t>(e.data.size())) {
                    const zip_int64_t r = zip_fread(zf, e.data.data() + off,
                                                    e.data.size() - static_cast<size_t>(off));
                    if (r <= 0) break;
                    off += r;
                }
                zip_fclose(zf);

                if (off >= 0 && static_cast<size_t>(off) < e.data.size())
                    e.data.resize(static_cast<size_t>(off));
            }

            entries.push_back(std::move(e));
        }

        zip_close(zin); // closes zin and owns inSrc

        // Build name list (for target detection)
        std::vector<std::string> names;
        names.reserve(entries.size());
        for (auto &e: entries) names.push_back(e.name);

        // ---- Find targets by ZIP entry name ----
        const std::vector<size_t> targets = getTargetEntryIndices(format, entries);
        if (targets.empty())
            return {false, "❌ No target fragments found in archive for this format.", {}};

        // ---- Convert targets ----
        size_t convertedCount = 0;

        for (const size_t idx: targets) {
            if (idx >= entries.size()) continue;
            Entry &e = entries[idx];
            if (e.isDir) continue;

            // Interpret as UTF-8 text (XML/XHTML)
            std::string text(e.data.begin(), e.data.end());

            std::map<std::string, std::string> fontMap;
            if (keepFont)
                maskFont(text, format, fontMap);

            std::string converted = helper.convert_cfg(text, config, punctuation);

            if (keepFont && !fontMap.empty()) {
                for (auto &[key, val]: fontMap) {
                    const std::string &marker = key;
                    const std::string &original = val;

                    size_t pos;
                    while ((pos = converted.find(marker)) != std::string::npos)
                        converted.replace(pos, marker.length(), original);
                }
            }

            e.data.assign(converted.begin(), converted.end());
            ++convertedCount;
        }

        if (convertedCount == 0)
            return {false, "❌ No fragments were converted. Nothing changed.", {}};

        // ---- Create output ZIP to memory ----
        zip_error_t outErr;
        zip_error_init(&outErr);

        zip_source_t *outSrc = zip_source_buffer_create(nullptr, 0, 0, &outErr);
        if (!outSrc) {
            zip_error_fini(&outErr);
            return {false, "❌ Failed to create output ZIP buffer source.", {}};
        }

        // Keep external reference so we can read bytes after zip_close()
        zip_source_keep(outSrc);

        zip_t *z_out = zip_open_from_source(outSrc, ZIP_CREATE | ZIP_TRUNCATE, &outErr);
        if (!z_out) {
            zip_source_free(outSrc);
            zip_error_fini(&outErr);
            return {false, "❌ Failed to open output ZIP from buffer source.", {}};
        }

        auto addDir = [&](const std::string &entryName) -> bool {
            // libzip expects directory entries ending with '/'
            std::string nm = entryName;
            if (nm.empty()) return true;
            if (nm.back() != '/') nm.push_back('/');

            // ZIP_FL_ENC_UTF_8 ensures UTF-8 names
            const zip_int64_t r = zip_dir_add(z_out, nm.c_str(), ZIP_FL_ENC_UTF_8);
            return r >= 0;
        };

        auto addFile = [&](const std::string &entryName,
                           const std::vector<uint8_t> &data,
                           const bool storeNoCompress) -> bool {
            zip_source_t *s = zip_source_buffer(z_out, data.data(), data.size(), 0);
            if (!s) return false;

            const zip_int64_t fileIndex = zip_file_add(
                z_out,
                entryName.c_str(),
                s,
                ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE
            );

            if (fileIndex < 0) {
                zip_source_free(s);
                return false;
            }

            if (storeNoCompress) {
                // ZIP_CM_STORE = no compression
                zip_set_file_compression(z_out, static_cast<zip_uint64_t>(fileIndex), ZIP_CM_STORE, 0);
            }
            return true;
        };

        // EPUB requirement: "mimetype" must be first and stored (no compression) if present
        auto isMimeName = [](const std::string &nm) -> bool {
            return nm == "mimetype" || nm == "./mimetype" || nm == "/mimetype";
        };

        bool addedMime = false;
        if (format == "epub") {
            for (auto &[name, isDir, data]: entries) {
                if (isDir) continue;
                if (!isMimeName(name)) continue;

                // Normalize to exactly "mimetype"
                if (!addFile("mimetype", data, /*storeNoCompress*/ true)) {
                    zip_discard(z_out);
                    zip_source_free(outSrc);
                    zip_error_fini(&outErr);
                    return {false, "❌ Failed to add EPUB mimetype entry.", {}};
                }
                addedMime = true;
                break;
            }
        }

        // Add remaining entries in original order (skip mimetype if already added)
        for (const auto &[name, isDir, data]: entries) {
            if (addedMime && isMimeName(name))
                continue;

            if (isDir) {
                if (!addDir(name)) {
                    zip_discard(z_out);
                    zip_source_free(outSrc);
                    zip_error_fini(&outErr);
                    return {false, "❌ Failed to add directory entry: " + name, {}};
                }
                continue;
            }

            if (!addFile(name, data, /*storeNoCompress*/ false)) {
                zip_discard(z_out);
                zip_source_free(outSrc);
                zip_error_fini(&outErr);
                return {false, "❌ Failed to add file to output ZIP: " + name, {}};
            }
        }

        if (zip_close(z_out) != 0) {
            zip_source_free(outSrc);
            zip_error_fini(&outErr);
            return {false, "❌ Failed to finalize output ZIP.", {}};
        }

        // ---- Read bytes back from outSrc ----
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
            "✅ Conversion completed.\n✅ Converted " + std::to_string(convertedCount) + " fragment(s).\n",
            std::move(outBytes)
        };
    }

private:
    struct Entry {
        std::string name;
        bool isDir = false;
        std::vector<uint8_t> data; // empty for directories
    };

    static inline bool IsSafeZipEntryName(const std::string& name) {
        if (name.empty()) return false;
        if (name[0] == '/' || name[0] == '\\') return false;
        if (name.find('\0') != std::string::npos) return false;
        if (name.find('\\') != std::string::npos) return false; // normalize or reject
        if (name.find("..") != std::string::npos) {
            // stricter: reject any "../" path segment
            if (name.find("../") != std::string::npos || name.find("/..") != std::string::npos)
                return false;
        }
        return true;
    }


    static inline std::string toLowerCopy(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static inline bool endsWith(const std::string &s, const std::string &suf) {
        return s.size() >= suf.size() &&
               s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
    }

    // Decide which ZIP entries are targets for conversion
    static std::vector<size_t> getTargetEntryIndices(const std::string &format,
                                                     const std::vector<Entry> &entries) {
        std::vector<size_t> result;
        result.reserve(32);

        if (format == "docx") {
            for (size_t i = 0; i < entries.size(); ++i)
                if (!entries[i].isDir && entries[i].name == "word/document.xml")
                    result.push_back(i);
        } else if (format == "xlsx") {
            for (size_t i = 0; i < entries.size(); ++i)
                if (!entries[i].isDir && entries[i].name == "xl/sharedStrings.xml")
                    result.push_back(i);
        } else if (format == "pptx") {
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].isDir) continue;
                const std::string &nm = entries[i].name;
                if (nm.rfind("ppt/", 0) != 0) continue;
                if (!endsWith(nm, ".xml")) continue;

                // slides/slide*.xml OR notesSlides/notesSlide*.xml
                if (nm.find("slides/slide") != std::string::npos ||
                    nm.find("notesSlides/notesSlide") != std::string::npos) {
                    result.push_back(i);
                }
            }
        } else if (format == "odt" || format == "ods" || format == "odp") {
            for (size_t i = 0; i < entries.size(); ++i)
                if (!entries[i].isDir && entries[i].name == "content.xml")
                    result.push_back(i);
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

    // Same mask logic as your original file version
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
            pattern = std::regex(R"((font-family\s*:\s*)([^;]+)(;?))");
        else
            return;

        std::smatch match;
        size_t counter = 0;
        std::string result;
        auto begin = xml.cbegin();
        const auto end = xml.cend();

        while (std::regex_search(begin, end, match, pattern)) {
            std::string marker = "__F_O_N_T_" + std::to_string(counter++) + "__";
            fontMap[marker] = match[2];

            result.append(match.prefix());
            result.append(match[1]);
            result.append(marker);
            if (match.size() > 3) result.append(match[3]);

            begin = match.suffix().first;
        }

        result.append(begin, end);
        xml = result;
    }
};
