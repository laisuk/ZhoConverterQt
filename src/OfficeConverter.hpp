#pragma once

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>
#include <zip.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

// The callback receives/returns UTF-8 and owns all text policy (for example,
// NormalizeCompat -> engine -> DeToFu). It must stay valid for the entire call.
// Calls are sequential. Exceptions become failure results and never commit output.
using TextConverter = std::function<std::string(std::string_view)>;

class OfficeConverter {
public:
    using TextConverter = ::TextConverter;
    static inline const std::unordered_set<std::string> OFFICE_EXTENSIONS = {
        "docx", "xlsx", "pptx", "odt",
        "ods", "odp", "epub"
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

    // maxTargetEntryBytes optionally caps each decompressed input XML/XHTML
    // entry (not callback allocations or total archive work). Default: no cap.
    // Paths are UTF-8, including on Windows. The temporary archive is finalized
    // and validated before replacement. Unix rename replaces atomically on the
    // same filesystem; Windows uses MoveFileExW with no atomicity guarantee.
    // No delete/copy fallback, attribute changes, or power-loss durability.
    // Cleanup is best effort and cannot cover abrupt process termination.
    static Result Convert(const std::string &inputPath, const std::string &outputPath,
                          const std::string &format, const TextConverter &converter, const bool keepFont = false,
                          const uint64_t maxTargetEntryBytes = (std::numeric_limits<uint64_t>::max)()) {
        try {
            const auto fmt = checkArguments(format, converter);
            const auto input = utf8Path(inputPath);
            const auto output = utf8Path(outputPath);
            if (std::error_code ec; std::filesystem::equivalent(input, output, ec))
                throw std::runtime_error("Input and output refer to the same file.");
            const auto zin = openFile(input, ZIP_RDONLY | ZIP_CHECKCONS);
            TempOutput temp(output);
            const TempOutput spool(output);
            auto zout = openFile(temp.path, ZIP_CREATE | ZIP_TRUNCATE);
            const auto count = convertArchive(zin.get(), zout.get(), fmt, converter, keepFont, &spool.path,
                                              maxTargetEntryBytes);
            finish(zout); {
                const auto valid = openFile(temp.path, ZIP_RDONLY | ZIP_CHECKCONS);
                validate(valid.get());
            }
            temp.replace(output);
            return {.success = true, .message = message(count)};
        } catch (const std::exception &e) {
            return {.success = false, .message = e.what()};
        } catch (...) {
            return {.success = false, .message = "Unknown Office conversion failure."};
        }
    }

    static BytesResult ConvertBytes(const std::vector<uint8_t> &bytes, const std::string &format,
                                    const TextConverter &converter, const bool keepFont = false,
                                    const uint64_t maxTargetEntryBytes = (std::numeric_limits<uint64_t>::max)()) {
        try {
            const auto fmt = checkArguments(format, converter);
            if (bytes.empty())
                throw std::runtime_error("Input ZIP buffer is empty.");
            Error error;
            Source in(zip_source_buffer_create(bytes.data(), bytes.size(), 0, &error.value));
            const auto zin = openSource(in, ZIP_RDONLY | ZIP_CHECKCONS, error);
            const Source out(zip_source_buffer_create(nullptr, 0, 0, &error.value));
            if (!out)
                throw std::runtime_error(error.text());
            zip_source_keep(out.get());
            Source owned(out.get());
            auto zout = openSource(owned, ZIP_CREATE | ZIP_TRUNCATE, error);
            const auto count =
                    convertArchive(zin.get(), zout.get(), fmt, converter, keepFont, nullptr, maxTargetEntryBytes);
            finish(zout);
            if (zip_source_open(out.get()) < 0)
                throw std::runtime_error("Cannot read output ZIP.");
            struct CloseSource {
                zip_source_t *source;
                ~CloseSource() { zip_source_close(source); }
            } close{out.get()};
            std::vector<uint8_t> result;
            std::array<uint8_t, 8192> buffer{};
            for (;;) {
                const auto n = zip_source_read(out.get(), buffer.data(), buffer.size());
                if (n < 0)
                    throw std::runtime_error("Failed reading output ZIP.");
                if (!n)
                    break;
                result.insert(result.end(), buffer.begin(), buffer.begin() + n);
            }
            Source verify(zip_source_buffer_create(result.data(), result.size(), 0, &error.value));
            const auto valid = openSource(verify, ZIP_RDONLY | ZIP_CHECKCONS, error);
            validate(valid.get());
            return {.success = true, .message = message(count), .outputBytes = std::move(result)};
        } catch (const std::exception &e) {
            return {.success = false, .message = e.what(), .outputBytes = {}};
        } catch (...) {
            return {.success = false, .message = "Unknown Office conversion failure.", .outputBytes = {}};
        }
    }

private:
    friend struct OfficeConverterTestAccess;

    static std::filesystem::path utf8Path(const std::string_view text) {
        return {std::u8string(text.begin(), text.end())};
    }

    struct Error {
        zip_error_t value{};
        Error() { zip_error_init(&value); }
        ~Error() { zip_error_fini(&value); }
        std::string text() { return zip_error_strerror(&value); }
    };

    struct Discard {
        void operator()(zip_t *p) const { zip_discard(p); }
    };

    struct FreeSource {
        void operator()(zip_source_t *p) const { zip_source_free(p); }
    };

    struct CloseFile {
        void operator()(zip_file_t *p) const { zip_fclose(p); }
    };

    using Archive = std::unique_ptr<zip_t, Discard>;
    using Source = std::unique_ptr<zip_source_t, FreeSource>;
    using File = std::unique_ptr<zip_file_t, CloseFile>;

    static Archive openSource(Source &source, const int flags, Error &error) {
        if (!source)
            throw std::runtime_error(error.text());
        Archive archive(zip_open_from_source(source.get(), flags, &error.value));
        if (!archive)
            throw std::runtime_error(error.text());
        [[maybe_unused]] auto *transferredSource = source.release(); // libzip now owns the source.
        return archive;
    }

    static Archive openFile(const std::filesystem::path &path, const int flags) {
        Error error;
#ifdef _WIN32
        Source source(zip_source_win32w_create(path.c_str(), 0, -1, &error.value));
#else
        Source source(zip_source_file_create(path.c_str(), 0, -1, &error.value));
#endif
        return openSource(source, flags, error);
    }

    static void finish(Archive &archive) {
        if (zip_close(archive.get()) != 0)
            throw std::runtime_error(std::string("Cannot finalize ZIP: ") + zip_strerror(archive.get()));
        [[maybe_unused]] auto *closedArchive = archive.release(); // Successful zip_close already freed the handle.
    }

    struct TempOutput {
        std::filesystem::path path;
        bool committed = false;

        explicit TempOutput(const std::filesystem::path &destination) {
            std::random_device random;
            for (unsigned i = 0; i < 128; ++i) {
                auto candidate = destination.parent_path() / (".office-" + std::to_string(random()) + "-" +
                                                              std::to_string(random()) + ".tmp");
#ifdef _WIN32
                HANDLE handle = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                            FILE_ATTRIBUTE_NORMAL, nullptr);
                if (handle != INVALID_HANDLE_VALUE) {
                    CloseHandle(handle);
                    path = std::move(candidate);
                    return;
                }
                if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS)
                    throw std::runtime_error("Cannot create output temporary file (Windows error " +
                                             std::to_string(GetLastError()) + ").");
#else
                const int fd = ::open(candidate.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
                if (fd >= 0) {
                    ::close(fd);
                    path = std::move(candidate);
                    return;
                }
                if (errno != EEXIST)
                    throw std::runtime_error("Cannot create output temporary file.");
#endif
            }
            throw std::runtime_error("Cannot allocate unique output temporary file.");
        }

        TempOutput(const TempOutput &) = delete;

        TempOutput &operator=(const TempOutput &) = delete;

        ~TempOutput() {
            if (!committed) {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        }

        void replace(const std::filesystem::path &destination) {
#ifdef _WIN32
            if (!MoveFileExW(path.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING))
                throw std::runtime_error("Cannot replace destination (Windows error " +
                                         std::to_string(GetLastError()) + ").");
#else
            std::filesystem::rename(path, destination);
#endif
            committed = true;
        }
    };

    static std::string toLowerCopy(std::string s) {
        for (auto &c: s)
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
        return s;
    }

    static std::string checkArguments(const std::string &format, const TextConverter &converter) {
        auto fmt = toLowerCopy(format);
        if (!OFFICE_EXTENSIONS.contains(fmt))
            throw std::runtime_error("Unsupported Office format: " + format);
        if (!converter)
            throw std::runtime_error("TextConverter is empty.");
        return fmt;
    }

    static std::string normalizedName(std::string name) {
        std::ranges::replace(name, '\\', '/');
        return name;
    }

    static bool safeName(const std::string &name) {
        if (name.empty() || name.front() == '/' || name.find('\0') != std::string::npos ||
            (name.size() >= 2 && name[1] == ':'))
            return false;
        size_t pos = 0;
        while (pos < name.size()) {
            const auto end = name.find('/', pos);
            const auto component =
                    std::string_view(name).substr(pos, end == std::string::npos ? end : end - pos);
            // A single final slash denotes a directory. Empty interior
            // components and "." would create alternate spellings of a path.
            if (component.empty() || component == "." || component == "..")
                return false;
            if (end == std::string::npos)
                break;
            pos = end + 1;
        }
        return true;
    }

    static bool isXlsxSharedStrings(const std::string &name) { return name == "xl/sharedstrings.xml"; }

    static bool isXlsxWorksheet(const std::string &name) {
        return name.starts_with("xl/worksheets/") && name.ends_with(".xml") &&
               name.find("/_rels/") == std::string::npos;
    }

    static bool target(const std::string &fmt, const std::string &name) {
        if (name.ends_with('/') || name.find("/_rels/") != std::string::npos)
            return false;
        if (fmt == "docx")
            return name == "word/document.xml";
        if (fmt == "xlsx")
            return isXlsxSharedStrings(name) || isXlsxWorksheet(name);
        if (fmt == "pptx") {
            if (!name.ends_with(".xml"))
                return false;
            return name.starts_with("ppt/slides/") || name.starts_with("ppt/notesslides/") ||
                   name.starts_with("ppt/slidemasters/") || name.starts_with("ppt/slidelayouts/") ||
                   name.starts_with("ppt/comments/") || name == "ppt/commentauthors.xml";
        }
        if (fmt == "epub")
            return name.ends_with(".xhtml") || name.ends_with(".html") || name.ends_with(".opf") ||
                   name.ends_with(".ncx");
        return name == "content.xml";
    }

    static bool validUtf8(const std::string_view text) {
        size_t pos = 0;
        while (pos < text.size()) {
            const auto lead = static_cast<unsigned char>(text[pos++]);
            if (lead < 0x80)
                continue;
            unsigned remaining;
            uint32_t code;
            uint32_t minimum;
            if (lead >= 0xc2 && lead <= 0xdf) {
                remaining = 1;
                code = lead & 0x1f;
                minimum = 0x80;
            } else if (lead >= 0xe0 && lead <= 0xef) {
                remaining = 2;
                code = lead & 0x0f;
                minimum = 0x800;
            } else if (lead >= 0xf0 && lead <= 0xf4) {
                remaining = 3;
                code = lead & 0x07;
                minimum = 0x10000;
            } else
                return false;
            if (remaining > text.size() - pos)
                return false;
            while (remaining--) {
                const auto next = static_cast<unsigned char>(text[pos++]);
                if ((next & 0xc0) != 0x80)
                    return false;
                code = (code << 6) | (next & 0x3f);
            }
            if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
                return false;
        }
        return true;
    }

    // Read through EOF to check CRC, decompression errors, and declared size.
    static std::string readEntry(zip_t *archive, const zip_uint64_t index,
                                 const uint64_t maxBytes = (std::numeric_limits<uint64_t>::max)()) {
        zip_stat_t stat{};
        if (zip_stat_index(archive, index, 0, &stat) < 0)
            throw std::runtime_error(zip_strerror(archive));
        std::string data;
        if (!(stat.valid & ZIP_STAT_SIZE))
            throw std::runtime_error("ZIP entry has no declared size.");
        if (stat.size > maxBytes || stat.size > data.max_size())
            throw std::runtime_error("ZIP entry exceeds maximum target-entry size.");
        File file(zip_fopen_index(archive, index, 0));
        if (!file)
            throw std::runtime_error(zip_strerror(archive));
        std::array<char, 8192> buffer{};
        for (;;) {
            const auto n = zip_fread(file.get(), buffer.data(), buffer.size());
            if (n < 0)
                throw std::runtime_error(zip_file_strerror(file.get()));
            if (!n)
                break;
            // Check before appending: neither a false header nor arithmetic
            // wraparound may bypass the bound.
            if (const auto bytes = static_cast<zip_uint64_t>(n);
                bytes > stat.size - data.size() || bytes > maxBytes - data.size())
                throw std::runtime_error("ZIP entry exceeds declared or maximum target-entry size.");
            data.append(buffer.data(), static_cast<size_t>(n));
        }
        if (data.size() != stat.size)
            throw std::runtime_error("Truncated ZIP entry.");
        if (zip_fclose(file.release()) < 0)
            throw std::runtime_error("Cannot close ZIP entry.");
        return data;
    }

    static void validate(zip_t *archive) {
        const auto count = zip_get_num_entries(archive, 0);
        if (count <= 0)
            throw std::runtime_error("Empty or invalid completed ZIP.");
        for (zip_int64_t i = 0; i < count; ++i) {
            zip_stat_t stat{};
            if (zip_stat_index(archive, i, 0, &stat) < 0 || !(stat.valid & ZIP_STAT_SIZE))
                throw std::runtime_error("Cannot determine completed ZIP entry size.");
            File file(zip_fopen_index(archive, i, 0));
            if (!file)
                throw std::runtime_error(zip_strerror(archive));
            std::array<char, 8192> buffer{};
            zip_uint64_t total = 0;
            for (;;) {
                const auto n = zip_fread(file.get(), buffer.data(), buffer.size());
                if (n < 0)
                    throw std::runtime_error(zip_file_strerror(file.get()));
                if (!n)
                    break;
                const auto bytes = static_cast<zip_uint64_t>(n);
                if (bytes > stat.size - total)
                    throw std::runtime_error("Completed ZIP entry exceeds declared size.");
                total += bytes;
            }
            if (total != stat.size)
                throw std::runtime_error("Truncated completed ZIP entry.");
            if (zip_fclose(file.release()) < 0)
                throw std::runtime_error("Cannot close validated entry.");
        }
    }

    static void addBuffer(zip_t *out, const std::string &name, const std::string &data) {
        // libzip consumes sources at zip_close, so transfer an owned allocation.
        void *buffer = std::malloc(std::max<size_t>(1, data.size()));
        if (!buffer)
            throw std::bad_alloc();
        std::memcpy(buffer, data.data(), data.size());
        Source source(zip_source_buffer(out, buffer, data.size(), 1));
        if (!source) {
            std::free(buffer);
            throw std::runtime_error(zip_strerror(out));
        }
        if (zip_file_add(out, name.c_str(), source.get(), ZIP_FL_ENC_UTF_8) < 0)
            throw std::runtime_error(zip_strerror(out));
        [[maybe_unused]] auto *transferredSource = source.release(); // Ownership transferred to the output archive.
    }

    static size_t convertArchive(zip_t *in, zip_t *out, const std::string &fmt,
                                 const TextConverter &converter, bool keepFont,
                                 const std::filesystem::path *spool = nullptr,
                                 uint64_t maxTargetEntryBytes = (std::numeric_limits<uint64_t>::max)()) {
        zip_uint64_t spoolOffset = 0;
        const auto count = zip_get_num_entries(in, 0);
        if (count < 0)
            throw std::runtime_error(zip_strerror(in));
        // Validate the entire name table before callbacks or special EPUB
        // handling. ZIP paths are byte strings, independent of host filesystem
        // normalization; case remains significant for collision detection.
        zip_int64_t mime = -1;
        std::unordered_set<std::string> names;
        for (zip_int64_t i = 0; i < count; ++i) {
            const char *raw = zip_get_name(in, i, 0);
            if (!raw)
                throw std::runtime_error(zip_strerror(in));
            const auto name = normalizedName(raw);
            if (!safeName(name))
                throw std::runtime_error("Unsafe or ambiguous ZIP entry name: " + name);
            // A file and directory with the same path are aliases too.
            if (const auto key = name.ends_with('/') ? name.substr(0, name.size() - 1) : name; !names.insert(key).
                second)
                throw std::runtime_error("Duplicate normalized ZIP entry: " + name);
            if (fmt == "epub" && name == "mimetype")
                mime = i;
        }
        if (fmt == "epub") {
            if (mime < 0 || readEntry(in, mime, 20) != "application/epub+zip")
                throw std::runtime_error("EPUB requires a valid mimetype entry.");
            addBuffer(out, "mimetype", "application/epub+zip");
            if (zip_set_file_compression(out, 0, ZIP_CM_STORE, 0) < 0)
                throw std::runtime_error(zip_strerror(out));
        }
        size_t converted = 0;
        for (zip_int64_t i = 0; i < count; ++i) {
            if (i == mime)
                continue;
            const char *raw = zip_get_name(in, i, 0);
            if (!raw)
                throw std::runtime_error(zip_strerror(in));
            const auto name = normalizedName(raw);
            const auto lower = toLowerCopy(name);
            if (target(fmt, lower)) {
                auto text = readEntry(in, i, maxTargetEntryBytes);
                if (!validUtf8(text))
                    throw std::runtime_error("Target entry is not UTF-8: " + name);
                std::map<std::string, std::string> fonts;
                if (keepFont && (fmt != "xlsx" || isXlsxSharedStrings(lower)))
                    maskFont(text, fmt, fonts);
                auto result = fmt == "xlsx" && isXlsxWorksheet(lower)
                                  ? convertXlsxInlineStrings(text, converter)
                                  : converter(text);
                if (!validUtf8(result))
                    throw std::runtime_error("TextConverter returned invalid UTF-8.");
                for (const auto &[marker, original]: fonts) {
                    size_t pos = 0;
                    while ((pos = result.find(marker, pos)) != std::string::npos) {
                        result.replace(pos, marker.size(), original);
                        pos += original.size();
                    }
                }
                if (spool && !result.empty()) {
                    constexpr auto maxSpoolBytes =
                            (std::min)(static_cast<uint64_t>((std::numeric_limits<zip_int64_t>::max)()),
                                       static_cast<uint64_t>((std::numeric_limits<std::streamsize>::max)()));
                    // The invariant spoolOffset <= maxSpoolBytes makes this
                    // subtraction and the subsequent addition safe.
                    if (result.size() > maxSpoolBytes - spoolOffset)
                        throw std::runtime_error("Converted entries exceed spool size range.");
                    // File conversions retain only one transformed entry in RAM.
                    // Sources read disjoint ranges of this owned spool at close.
                    {
                        std::ofstream stream(*spool, std::ios::binary | std::ios::app);
                        stream.write(result.data(), static_cast<std::streamsize>(result.size()));
                        stream.close();
                        if (!stream)
                            throw std::runtime_error("Cannot write conversion spool.");
                    }
                    Error error;
#ifdef _WIN32
                    Source source(
                        zip_source_win32w_create(spool->c_str(), spoolOffset, static_cast<zip_int64_t>(result.size()),
                                                 &error.value));
#else
                    Source source(
                        zip_source_file_create(spool->c_str(), spoolOffset, result.size(), &error.value));
#endif
                    if (!source)
                        throw std::runtime_error(error.text());
                    if (zip_file_add(out, name.c_str(), source.get(), ZIP_FL_ENC_UTF_8) < 0)
                        throw std::runtime_error(zip_strerror(out));
                    [[maybe_unused]] auto *transferredSource = source.release();
                    // Ownership transferred to the output archive.
                    spoolOffset += result.size();
                } else {
                    addBuffer(out, name, result);
                }
                ++converted;
            } else {
                // Keep input alive until output finalization; copy compressed bytes.
#if LIBZIP_VERSION_MAJOR > 1 || LIBZIP_VERSION_MINOR >= 10
                Source source(zip_source_zip_file(out, in, i, ZIP_FL_COMPRESSED, 0, -1, nullptr));
#else
                // Legacy API implicitly requests compressed data for the full
                // entry (start=0, len=-1); no ZIP_FL_COMPRESSED flag is needed.
                Source source(zip_source_zip(out, in, i, 0, 0, -1));
#endif
                if (!source || zip_file_add(out, name.c_str(), source.get(), ZIP_FL_ENC_UTF_8) < 0)
                    throw std::runtime_error(zip_strerror(out));
                [[maybe_unused]] auto *transferredSource = source.release();
                // Ownership transferred to the output archive.
            }
            const auto idx = static_cast<zip_uint64_t>(zip_get_num_entries(out, 0) - 1);
            zip_stat_t stat{};
            if (zip_stat_index(in, i, 0, &stat) < 0)
                throw std::runtime_error(zip_strerror(in));
            // ZIP_CM_DEFAULT can otherwise deflate an originally stored entry,
            // even when the source supplies its original compressed bytes.
            if (!target(fmt, lower) && (stat.valid & ZIP_STAT_COMP_METHOD) &&
                zip_set_file_compression(out, idx, stat.comp_method, 0) < 0)
                throw std::runtime_error(zip_strerror(out));
            if ((stat.valid & ZIP_STAT_MTIME) && zip_file_set_mtime(out, idx, stat.mtime, 0) < 0)
                throw std::runtime_error(zip_strerror(out));
            zip_uint8_t os;
            zip_uint32_t attributes;
            if (zip_file_get_external_attributes(in, i, 0, &os, &attributes) == 0 &&
                zip_file_set_external_attributes(out, idx, 0, os, attributes) < 0)
                throw std::runtime_error(zip_strerror(out));
            zip_uint32_t length = 0;
            if (const char *comment = zip_file_get_comment(in, i, &length, 0); comment && zip_file_set_comment(
                                                                                   out, idx, comment,
                                                                                   static_cast<zip_uint16_t>(length),
                                                                                   0) < 0)
                throw std::runtime_error(zip_strerror(out));
        }
        if (!converted)
            throw std::runtime_error("No target fragments found in archive for this format.");
        int length = 0;
        if (const char *comment = zip_get_archive_comment(in, &length, 0); comment && zip_set_archive_comment(
                                                                               out, comment,
                                                                               static_cast<zip_uint16_t>(length)) < 0)
            throw std::runtime_error(zip_strerror(out));
        return converted;
    }

    static std::string message(const size_t count) {
        return "Conversion completed. Converted " + std::to_string(count) + " fragment(s).";
    }

    static std::string convertXlsxInlineStrings(const std::string &xml, const TextConverter &converter) {
        static const std::regex cellPattern(R"(<c\b[^>]*>[\s\S]*?<\/c>)", std::regex::ECMAScript);

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
                const bool isInlineStr = openTag.find(R"(t="inlineStr")") != std::string::npos ||
                                         openTag.find(R"(t='inlineStr')") != std::string::npos;

                if (isInlineStr) {
                    out.append(convertXlsxInlineStringCell(cellXml, converter));
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
                                                   const TextConverter &converter) {
        static const std::regex textNodePattern(R"((<t\b[^>]*>)([\s\S]*?)(<\/t>))", std::regex::ECMAScript);

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
                const std::string convertedText = converter(innerText);
                out.append(openTag);
                out.append(convertedText);
                out.append(closeTag);
            }

            last = m.suffix().first;
        }

        out.append(last, cellXml.cend());
        return out;
    }

    static void maskFont(std::string &xml, const std::string &format,
                         std::map<std::string, std::string> &fontMap) {
        const std::regex *pattern = nullptr;
        if (format == "docx") {
            static const std::regex cached(R"((w:(?:eastAsia|ascii|hAnsi|cs)=\")(.*?)(\"))");
            pattern = &cached;
        } else if (format == "xlsx") {
            static const std::regex cached(R"((val=\")(.*?)(\"))");
            pattern = &cached;
        } else if (format == "pptx") {
            static const std::regex cached(R"((typeface=\")(.*?)(\"))");
            pattern = &cached;
        } else if (format == "odt" || format == "ods" || format == "odp") {
            static const std::regex cached(
                R"(((?:style:font-name(?:-asian|-complex)?|svg:font-family|style:name)=[\'\"])([^\'\"]+)([\'\"]))");
            pattern = &cached;
        } else if (format == "epub") {
            static const std::regex cached(R"((font-family\s*:\s*)([^;\"']+)([;\"']?))");
            pattern = &cached;
        } else
            return;

        std::smatch match;
        size_t counter = 0;
        // Pick a deterministic prefix absent from the entire original XML.
        // This also excludes collisions with original font values.
        std::string prefix = "__F_O_N_T_";
        while (xml.find(prefix) != std::string::npos)
            prefix.push_back('_');
        std::string result;
        result.reserve(xml.size());

        auto begin = xml.cbegin();
        const auto end = xml.cend();

        while (std::regex_search(begin, end, match, *pattern)) {
            const std::string marker = prefix + std::to_string(counter++) + "__";
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
