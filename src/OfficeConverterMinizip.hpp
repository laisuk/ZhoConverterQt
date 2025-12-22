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
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
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
        std::vector<uint8_t> outputBytes; // valid if success==true
    };

    // ------------------------- Backward compatible File IO API -------------------------
    // File IO wrapper: Read file -> ConvertBytes(core) -> Write file
    static Result Convert(const std::string &inputPath,
                          const std::string &outputPath,
                          const std::string &format,
                          OpenccFmmsegHelper &helper,
                          const std::string &config,
                          bool punctuation,
                          bool keepFont = false) {
        // Read input file as bytes
        std::ifstream in(inputPath, std::ios::binary);
        if (!in) return {false, "❌ Cannot open input file."};

        std::vector<uint8_t> inputBytes{
            std::istreambuf_iterator<char>(in),
            std::istreambuf_iterator<char>()
        };

        auto [success, message, outputBytes] = ConvertBytes(inputBytes, format, helper, config, punctuation, keepFont);
        if (!success) return {false, message};

        // Ensure output directory exists? (caller may do it; we keep minimal here)
        std::ofstream out(outputPath, std::ios::binary);
        if (!out) return {false, "❌ Cannot open output file for writing."};

        out.write(reinterpret_cast<const char *>(outputBytes.data()),
                  static_cast<std::streamsize>(outputBytes.size()));

        return {true, message};
    }

    // ------------------------- In-memory ZIP Bytes Core (minizip-ng) -------------------------
    // Convert Office/EPUB ZIP bytes -> new ZIP bytes (no temp dir)
    static BytesResult ConvertBytes(const std::vector<uint8_t> &inputZipBytes,
                                    const std::string &format,
                                    OpenccFmmsegHelper &helper,
                                    const std::string &config,
                                    bool punctuation,
                                    bool keepFont = false) {
        if (inputZipBytes.empty())
            return {false, "❌ Input ZIP buffer is empty.", {}};

        std::ostringstream debug;

        // ---- Open input memory stream ----
        void *inStream = mz_stream_mem_create();
        if (!inStream)
            return {false, "❌ minizip: failed to create memory stream (input).", {}};

        mz_stream_mem_set_buffer(inStream,
                                 const_cast<uint8_t *>(inputZipBytes.data()),
                                 static_cast<int32_t>(inputZipBytes.size()));

        if (int32_t rc_s_open = mz_stream_open(inStream, nullptr, MZ_OPEN_MODE_READ); rc_s_open != MZ_OK) {
            mz_stream_mem_delete(&inStream);
            return {false, "❌ minizip: stream_open(READ) failed rc=" + std::to_string(rc_s_open), {}};
        }
        mz_stream_seek(inStream, 0, MZ_SEEK_SET);

        // ---- Open zip reader on stream ----
        void *reader = mz_zip_reader_create();
        if (!reader) {
            mz_stream_close(inStream);
            mz_stream_mem_delete(&inStream);
            return {false, "❌ minizip: failed to create zip reader.", {}};
        }

        if (int32_t rc_r_open = mz_zip_reader_open(reader, inStream); rc_r_open != MZ_OK) {
            mz_zip_reader_delete(&reader);
            mz_stream_close(inStream);
            mz_stream_mem_delete(&inStream);
            return {false, "❌ Failed to open ZIP archive from memory rc=" + std::to_string(rc_r_open), {}};
        }

        // ---- Read all entries ----
        std::vector<Entry> entries;
        entries.reserve(256);

        if (int32_t rc_goto = mz_zip_reader_goto_first_entry(reader); rc_goto != MZ_OK) {
            // If a ZIP is valid but empty, you'd still get not OK here. For Office/EPUB this is almost certainly error.
            mz_zip_reader_close(reader);
            mz_zip_reader_delete(&reader);
            mz_stream_close(inStream);
            mz_stream_mem_delete(&inStream);
            return {false, "❌ ZIP has no readable entries (rc=" + std::to_string(rc_goto) + ")", {}};
        }

        do {
            mz_zip_file *file_info = nullptr;
            mz_zip_reader_entry_get_info(reader, &file_info);
            if (!file_info || !file_info->filename)
                continue;

            Entry e;
            e.name = file_info->filename;

            // Normalize ZIP path separators (critical for OPC / Word)
            std::replace(e.name.begin(), e.name.end(), '\\', '/');

            // Gate unsafe names (best-effort: skip)
            if (!IsSafeZipEntryName(e.name))
                continue;

            e.isDir = (mz_zip_reader_entry_is_dir(reader) == MZ_OK) ||
                      (!e.name.empty() && e.name.back() == '/');

            if (e.isDir) {
                entries.push_back(std::move(e));
                continue;
            }

            // Read payload into our buffer (your headers expose: save_buffer(handle, void* buf, int32_t len))
            const auto usize64 = file_info->uncompressed_size;

            // Safety: cap to avoid pathological allocations (adjust if you want)
            if (constexpr int64_t MAX_ENTRY = 256LL * 1024LL * 1024LL; usize64 < 0 || usize64 > MAX_ENTRY) {
                debug << "minizip: skip unreasonable entry size (" << e.name << ") size=" << usize64 << "\n";
                entries.push_back(std::move(e)); // keep metadata; payload empty
                continue;
            }

            e.data.resize(static_cast<size_t>(usize64));

            if (int32_t rc_e_open = mz_zip_reader_entry_open(reader); rc_e_open != MZ_OK) {
                debug << "minizip: entry_open failed (" << e.name << ") rc=" << rc_e_open << "\n";
                e.data.clear();
                entries.push_back(std::move(e));
                continue;
            }

            int32_t rc_save = MZ_OK;
            if (!e.data.empty()) {
                // NOTE: minizip-ng uses int32_t length here; large entries above INT32_MAX are rejected by our cap anyway.
                rc_save = mz_zip_reader_entry_save_buffer(reader,
                                                          e.data.data(),
                                                          static_cast<int32_t>(e.data.size()));
            }

            mz_zip_reader_entry_close(reader);

            if (rc_save != MZ_OK) {
                debug << "minizip: save_buffer failed (" << e.name << ") rc=" << rc_save << "\n";
                e.data.clear(); // best-effort
            }

            entries.push_back(std::move(e));
        } while (mz_zip_reader_goto_next_entry(reader) == MZ_OK);

        // close reader + stream
        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);

        mz_stream_close(inStream);
        mz_stream_mem_delete(&inStream);

        if (entries.empty())
            return {false, "❌ ZIP has no readable entries.", {}};

        // ---- Decide targets by entry name ----
        const std::vector<size_t> targets = getTargetEntryIndices(format, entries);
        if (targets.empty())
            return {false, "❌ No target fragments found in archive for this format.", {}};

        // ---- Convert targets ----
        int convertedCount = 0;

        for (size_t idx: targets) {
            if (idx >= entries.size()) continue;

            Entry &e = entries[idx];
            if (e.isDir) continue;

            // Interpret as UTF-8 text (XML/XHTML)
            std::string xml(e.data.begin(), e.data.end());

            std::map<std::string, std::string> fontMap;
            if (keepFont) maskFont(xml, format, fontMap);

            std::string converted = helper.convert(xml, config, punctuation);

            if (keepFont) {
                for (auto &[key, val]: fontMap) {
                    const std::string &marker = key;
                    const std::string &original = val;

                    size_t pos;
                    while ((pos = converted.find(marker)) != std::string::npos)
                        converted.replace(pos, marker.length(), original);
                }
            }

            e.data.assign(converted.begin(), converted.end());
            convertedCount++;
        }

        if (convertedCount == 0)
            return {false, debug.str().empty() ? "❌ No fragments were converted." : debug.str(), {}};

        // ---- Create output memory stream ----
        void *outStream = mz_stream_mem_create();
        if (!outStream)
            return {false, "❌ minizip: failed to create memory stream (output).", {}};

        mz_stream_mem_set_grow_size(outStream, 64 * 1024);

        if (int32_t rc_out_open = mz_stream_open(outStream, nullptr, MZ_OPEN_MODE_CREATE); rc_out_open != MZ_OK) {
            mz_stream_mem_delete(&outStream);
            return {false, "❌ minizip: stream_open(CREATE) failed rc=" + std::to_string(rc_out_open), {}};
        }
        mz_stream_seek(outStream, 0, MZ_SEEK_SET);

        void *writer = mz_zip_writer_create();
        if (!writer) {
            mz_stream_close(outStream);
            mz_stream_mem_delete(&outStream);
            return {false, "❌ minizip: failed to create zip writer.", {}};
        }

        if (int32_t rc_w_open = mz_zip_writer_open(writer, outStream, 0); rc_w_open != MZ_OK) {
            mz_zip_writer_delete(&writer);
            mz_stream_close(outStream);
            mz_stream_mem_delete(&outStream);
            return {false, "❌ minizip: writer_open failed rc=" + std::to_string(rc_w_open), {}};
        }

        // defaults
        mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_DEFLATE);
        mz_zip_writer_set_compress_level(writer, MZ_COMPRESS_LEVEL_DEFAULT);

        auto isMimeName = [](const std::string &nm) -> bool {
            return nm == "mimetype" || nm == "./mimetype" || nm == "/mimetype";
        };

        bool ok = true;

        // EPUB: write mimetype first, stored (no compression)
        if (format == "epub") {
            for (const auto &[filename, isDir, data]: entries) {
                if (isDir) continue;
                if (!isMimeName(filename)) continue;

                mz_zip_file file_info = {};
                file_info.filename = "mimetype";
                file_info.flag |= MZ_ZIP_FLAG_UTF8;
                file_info.uncompressed_size = static_cast<int64_t>(data.size());
                file_info.compression_method = MZ_COMPRESS_METHOD_STORE;

                mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_STORE);
                mz_zip_writer_set_compress_level(writer, 0);

                const int32_t rc_add = mz_zip_writer_add_buffer(writer,
                                                                data.empty()
                                                                    ? nullptr
                                                                    : const_cast<uint8_t *>(data.data()),
                                                                static_cast<int32_t>(data.size()),
                                                                &file_info);

                // restore defaults
                mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_DEFLATE);
                mz_zip_writer_set_compress_level(writer, MZ_COMPRESS_LEVEL_DEFAULT);

                if (rc_add != MZ_OK) {
                    ok = false;
                    debug << "minizip: add_buffer(mimetype) failed rc=" << rc_add << "\n";
                }
                break;
            }
        }

        // Add all other files (directories are optional; we skip emitting them)
        if (ok) {
            for (const auto &[filename, isDir, data]: entries) {
                if (!IsSafeZipEntryName(filename))
                    continue;
                if (isDir)
                    continue;
                if (format == "epub" && isMimeName(filename))
                    continue;

                mz_zip_file file_info = {};
                file_info.filename = filename.c_str();
                file_info.flag |= MZ_ZIP_FLAG_UTF8;
                file_info.uncompressed_size = static_cast<int64_t>(data.size());
                file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;

                mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_DEFLATE);
                mz_zip_writer_set_compress_level(writer, MZ_COMPRESS_LEVEL_DEFAULT);

                const int32_t rc_add = mz_zip_writer_add_buffer(writer,
                                                                data.empty()
                                                                    ? nullptr
                                                                    : const_cast<uint8_t *>(data.data()),
                                                                static_cast<int32_t>(data.size()),
                                                                &file_info);

                if (rc_add != MZ_OK) {
                    // ok = false;
                    debug << "minizip: add_buffer(" << filename << ") failed rc=" << rc_add << "\n";
                    break;
                }
            }
        }

        //Fetch output bytes
        const int32_t rc_close = mz_zip_writer_close(writer);
        mz_zip_writer_delete(&writer);

        if (rc_close != MZ_OK) {
            mz_stream_close(outStream);
            mz_stream_mem_delete(&outStream);
            return {false, "minizip: writer_close failed rc=" + std::to_string(rc_close), {}};
        }

        // ---- Fetch output bytes from mem stream ----
        const void *out_buf = nullptr;
        mz_stream_mem_get_buffer(outStream, &out_buf);

        int32_t out_len32 = 0;
        mz_stream_mem_get_buffer_length(outStream, &out_len32);
        const int64_t out_len = out_len32;

        if (!out_buf || out_len <= 0) {
            mz_stream_close(outStream);
            mz_stream_mem_delete(&outStream);
            return {false, "❌ Output ZIP buffer is empty (unexpected).", {}};
        }

        // ✅ COPY WHILE STREAM IS STILL ALIVE
        std::vector<uint8_t> outBytes;
        outBytes.resize(static_cast<size_t>(out_len));
        std::memcpy(outBytes.data(), out_buf, static_cast<size_t>(out_len));

        std::string msg = "✅ Successfully converted " + std::to_string(convertedCount) + " fragment(s).";
        if (!debug.str().empty())
            msg += "\n" + debug.str();

        // Now it is safe to close/delete the stream
        mz_stream_close(outStream);
        mz_stream_mem_delete(&outStream);

        return {true, msg, std::move(outBytes)};
    }

private:
    struct Entry {
        std::string name;
        bool isDir = false;
        std::vector<uint8_t> data;
    };

    static inline std::string toLowerCopy(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static inline bool endsWith(const std::string &s, const std::string &suf) {
        return s.size() >= suf.size() &&
               s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
    }

    // Minimal gate (even though we don't extract to disk)
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

    // Decide targets by ZIP entry name (no filesystem)
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

    // Your original font-masking logic (unchanged)
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
