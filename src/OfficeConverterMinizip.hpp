#pragma once

#include "OpenccFmmsegHelper.hpp"

// minizip-ng
#include <minizip-ng/mz.h>
#include <minizip-ng/mz_zip.h>
#include <minizip-ng/mz_zip_rw.h>
#include <minizip-ng/mz_strm.h>
#include <minizip-ng/mz_strm_mem.h>

#include <algorithm>
#include <cctype>
// #include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

class OfficeConverterMinizip {
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
        if (!in)
            return {false, "❌ Cannot open input file."};

        std::vector<uint8_t> inputBytes{
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()
        };

        auto [success, message, outputBytes] =
                ConvertBytes(inputBytes, format, helper, config, punctuation, keepFont);
        if (!success)
            return {false, message};

        std::ofstream out(outputPath, std::ios::binary);
        if (!out)
            return {false, "❌ Cannot open output file for writing."};

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
        std::ostringstream debug;

        // -------------------------
        // Input stream / reader
        // -------------------------
        void *inStream = mz_stream_mem_create();
        if (!inStream)
            return {false, "❌ minizip: failed to create memory stream (input).", {}};

        auto cleanupInputStream = [&]() {
            if (inStream) {
                mz_stream_close(inStream);
                mz_stream_mem_delete(&inStream);
                inStream = nullptr;
            }
        };

        mz_stream_mem_set_buffer(
            inStream,
            const_cast<uint8_t *>(inputZipBytes.data()),
            static_cast<int32_t>(inputZipBytes.size())
        ); {
            if (const int32_t rc = mz_stream_open(inStream, nullptr, MZ_OPEN_MODE_READ); rc != MZ_OK) {
                cleanupInputStream();
                return {false, "❌ minizip: stream_open(READ) failed rc=" + std::to_string(rc), {}};
            }
        }

        mz_stream_seek(inStream, 0, MZ_SEEK_SET);

        void *reader = mz_zip_reader_create();
        if (!reader) {
            cleanupInputStream();
            return {false, "❌ minizip: failed to create zip reader.", {}};
        }

        auto cleanupReader = [&]() {
            if (reader) {
                mz_zip_reader_close(reader);
                mz_zip_reader_delete(&reader);
                reader = nullptr;
            }
        }; {
            if (const int32_t rc = mz_zip_reader_open(reader, inStream); rc != MZ_OK) {
                cleanupReader();
                cleanupInputStream();
                return {false, "❌ Failed to open ZIP archive from memory rc=" + std::to_string(rc), {}};
            }
        }

        // -------------------------
        // Read all entries
        // -------------------------
        std::vector<Entry> entries;
        entries.reserve(256); {
            if (const int32_t rcFirst = mz_zip_reader_goto_first_entry(reader); rcFirst != MZ_OK) {
                cleanupReader();
                cleanupInputStream();
                return {false, "❌ ZIP has no readable entries (rc=" + std::to_string(rcFirst) + ")", {}};
            }
        }

        do {
            mz_zip_file *fileInfo = nullptr;
            mz_zip_reader_entry_get_info(reader, &fileInfo);
            if (!fileInfo || !fileInfo->filename)
                continue;

            Entry e;
            e.name = fileInfo->filename;
            std::replace(e.name.begin(), e.name.end(), '\\', '/');

            if (!IsSafeZipEntryName(e.name))
                continue;

            e.isDir = (mz_zip_reader_entry_is_dir(reader) == MZ_OK) ||
                      (!e.name.empty() && e.name.back() == '/');

            if (e.isDir) {
                entries.push_back(std::move(e));
                continue;
            }

            const int64_t usize64 = fileInfo->uncompressed_size;
            if (constexpr int64_t MAX_ENTRY = 256LL * 1024LL * 1024LL; usize64 < 0 || usize64 > MAX_ENTRY) {
                debug << "minizip: skip unreasonable entry size (" << e.name << ") size=" << usize64 << "\n";
                entries.push_back(std::move(e));
                continue;
            }

            e.data.resize(static_cast<size_t>(usize64));

            if (const int32_t rcOpen = mz_zip_reader_entry_open(reader); rcOpen != MZ_OK) {
                debug << "minizip: entry_open failed (" << e.name << ") rc=" << rcOpen << "\n";
                e.data.clear();
                entries.push_back(std::move(e));
                continue;
            }

            int32_t rcSave = MZ_OK;
            if (!e.data.empty()) {
                rcSave = mz_zip_reader_entry_save_buffer(
                    reader,
                    e.data.data(),
                    static_cast<int32_t>(e.data.size())
                );
            }

            mz_zip_reader_entry_close(reader);

            if (rcSave != MZ_OK) {
                debug << "minizip: save_buffer failed (" << e.name << ") rc=" << rcSave << "\n";
                e.data.clear();
            }

            entries.push_back(std::move(e));
        } while (mz_zip_reader_goto_next_entry(reader) == MZ_OK);

        cleanupReader();
        cleanupInputStream();

        if (entries.empty())
            return {false, "❌ ZIP has no readable entries.", {}};

        // -------------------------
        // Target selection
        // -------------------------
        const std::vector<size_t> targets = getTargetEntryIndices(normalizedFormat, entries);
        if (targets.empty())
            return {false, "❌ No target fragments found in archive for this format.", {}};

        // -------------------------
        // Convert targets
        // -------------------------
        int convertedCount = 0;

        for (const size_t idx: targets) {
            if (idx >= entries.size())
                continue;

            auto &[entry, isDir, data] = entries[idx];
            if (isDir)
                continue;

            std::string xml(data.begin(), data.end());

            std::map<std::string, std::string> fontMap;
            if (keepFont && shouldMaskFonts(normalizedFormat, entry))
                maskFont(xml, normalizedFormat, fontMap);

            std::string converted;
            if (normalizedFormat == "xlsx") {
                converted = convertXlsxEntry(
                    xml,
                    entry,
                    helper,
                    config,
                    punctuation
                );
            } else {
                converted = helper.convert_cfg(xml, config, punctuation);
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
            return {false, debug.str().empty() ? "❌ No fragments were converted." : debug.str(), {}};

        // -------------------------
        // Output stream / writer
        // -------------------------
        void *outStream = mz_stream_mem_create();
        if (!outStream)
            return {false, "❌ minizip: failed to create memory stream (output).", {}};

        auto cleanupOutStream = [&]() {
            if (outStream) {
                mz_stream_close(outStream);
                mz_stream_mem_delete(&outStream);
                outStream = nullptr;
            }
        };

        mz_stream_mem_set_grow_size(outStream, 64 * 1024); {
            if (const int32_t rc = mz_stream_open(outStream, nullptr, MZ_OPEN_MODE_CREATE); rc != MZ_OK) {
                cleanupOutStream();
                return {false, "❌ minizip: stream_open(CREATE) failed rc=" + std::to_string(rc), {}};
            }
        }

        mz_stream_seek(outStream, 0, MZ_SEEK_SET);

        void *writer = mz_zip_writer_create();
        if (!writer) {
            cleanupOutStream();
            return {false, "❌ minizip: failed to create zip writer.", {}};
        }

        auto cleanupWriter = [&]() {
            if (writer) {
                mz_zip_writer_delete(&writer);
                writer = nullptr;
            }
        }; {
            if (const int32_t rc = mz_zip_writer_open(writer, outStream, 0); rc != MZ_OK) {
                cleanupWriter();
                cleanupOutStream();
                return {false, "❌ minizip: writer_open failed rc=" + std::to_string(rc), {}};
            }
        }

        mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_DEFLATE);
        mz_zip_writer_set_compress_level(writer, MZ_COMPRESS_LEVEL_DEFAULT);

        auto isMimeName = [](const std::string &nm) -> bool {
            return nm == "mimetype" || nm == "./mimetype" || nm == "/mimetype";
        };

        auto addBuffer = [&](const std::string &filename,
                             const std::vector<uint8_t> &data,
                             const int16_t compressionMethod,
                             const int16_t compressionLevel) -> int32_t {
            mz_zip_file fileInfo = {};
            fileInfo.filename = filename.c_str();
            fileInfo.flag |= MZ_ZIP_FLAG_UTF8;
            fileInfo.uncompressed_size = static_cast<int64_t>(data.size());
            fileInfo.compression_method = compressionMethod;

            mz_zip_writer_set_compress_method(writer, compressionMethod);
            mz_zip_writer_set_compress_level(writer, compressionLevel);

            return mz_zip_writer_add_buffer(
                writer,
                data.empty() ? nullptr : const_cast<uint8_t *>(data.data()),
                static_cast<int32_t>(data.size()),
                &fileInfo
            );
        };

        bool writeOk = true;

        // EPUB: write mimetype first, stored
        if (normalizedFormat == "epub") {
            bool foundMime = false;

            for (const auto &[filename, isDir, data]: entries) {
                if (isDir)
                    continue;
                if (!isMimeName(filename))
                    continue;

                foundMime = true;
                if (const int32_t rc = addBuffer("mimetype", data, MZ_COMPRESS_METHOD_STORE, 0); rc != MZ_OK) {
                    writeOk = false;
                    debug << "minizip: add_buffer(mimetype) failed rc=" << rc << "\n";
                }
                break;
            }

            if (!foundMime) {
                cleanupWriter();
                cleanupOutStream();
                return {false, "❌ 'mimetype' file is missing. EPUB requires it as the first entry.", {}};
            }
        }

        if (writeOk) {
            for (const auto &[filename, isDir, data]: entries) {
                if (!IsSafeZipEntryName(filename))
                    continue;
                if (isDir)
                    continue;
                if (normalizedFormat == "epub" && isMimeName(filename))
                    continue;

                const int32_t rc = addBuffer(
                    filename,
                    data,
                    MZ_COMPRESS_METHOD_DEFLATE,
                    MZ_COMPRESS_LEVEL_DEFAULT
                );

                if (rc != MZ_OK) {
                    writeOk = false;
                    debug << "minizip: add_buffer(" << filename << ") failed rc=" << rc << "\n";
                    break;
                }
            }
        }

        if (!writeOk) {
            cleanupWriter();
            cleanupOutStream();
            return {false, debug.str().empty() ? "❌ Failed to write output ZIP." : debug.str(), {}};
        }

        const int32_t rcClose = mz_zip_writer_close(writer);
        cleanupWriter();

        if (rcClose != MZ_OK) {
            cleanupOutStream();
            return {false, "❌ minizip: writer_close failed rc=" + std::to_string(rcClose), {}};
        }

        // IMPORTANT: copy bytes while outStream is still alive
        const void *outBuf = nullptr;
        mz_stream_mem_get_buffer(outStream, &outBuf);

        int32_t outLen32 = 0;
        mz_stream_mem_get_buffer_length(outStream, &outLen32);
        const int64_t outLen = outLen32;

        if (!outBuf || outLen <= 0) {
            cleanupOutStream();
            return {false, "❌ Output ZIP buffer is empty (unexpected).", {}};
        }

        std::vector<uint8_t> outBytes(static_cast<size_t>(outLen));
        std::memcpy(outBytes.data(), outBuf, static_cast<size_t>(outLen));

        cleanupOutStream();

        std::string msg =
                "✅ Successfully converted " + std::to_string(convertedCount) + " fragment(s).";
        if (!debug.str().empty())
            msg += "\n" + debug.str();

        return {true, msg, std::move(outBytes)};
    }

private:
    struct Entry {
        std::string name;
        bool isDir = false;
        std::vector<uint8_t> data;
    };

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

    static inline bool IsSafeZipEntryName(const std::string &name) {
        if (name.empty()) return false;
        if (name.front() == '/' || name.front() == '\\') return false;
        if (name.find('\0') != std::string::npos) return false;
        if (name.find('\\') != std::string::npos) return false;
        if (name.find("../") != std::string::npos) return false;
        if (name.find("..\\") != std::string::npos) return false;
        if (name.find("/..") != std::string::npos) return false;
        return true;
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
                if (entries[i].isDir)
                    continue;

                if (const std::string &nm = entries[i].name; isXlsxSharedStrings(nm) || isXlsxWorksheet(nm))
                    result.push_back(i);
            }
        } else if (format == "pptx") {
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].isDir)
                    continue;

                const std::string &nm = entries[i].name;
                if (nm.rfind("ppt/", 0) != 0)
                    continue;
                if (!endsWith(nm, ".xml"))
                    continue;

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
                if (entries[i].isDir)
                    continue;

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
            R"(<c\b(?=[^>]*\bt=(?:"inlineStr"|'inlineStr'))[^>]*>.*?<\/c>)",
            std::regex::ECMAScript
        );

        std::string out;
        out.reserve(xml.size());

        std::sregex_iterator it(xml.begin(), xml.end(), cellPattern);
        auto last = xml.begin();

        for (const std::sregex_iterator end; it != end; ++it) {
            const std::smatch &m = *it;
            out.append(last, m.prefix().second);
            out.append(convertXlsxInlineStringCell(m.str(), helper, config, punctuation));
            last = m.suffix().first;
        }

        out.append(last, xml.end());
        return out;
    }

    static std::string convertXlsxInlineStringCell(const std::string &cellXml,
                                                   const OpenccFmmsegHelper &helper,
                                                   const opencc_config_t &config,
                                                   const bool punctuation) {
        static const std::regex textNodePattern(
            R"((<t\b[^>]*>)(.*?)(<\/t>))",
            std::regex::ECMAScript
        );

        std::string out;
        out.reserve(cellXml.size());

        std::sregex_iterator it(cellXml.begin(), cellXml.end(), textNodePattern);
        auto last = cellXml.begin();

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

        out.append(last, cellXml.end());
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
