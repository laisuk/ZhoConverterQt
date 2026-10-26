#pragma once
#include "../../src/OfficeConverter.hpp"
#include <future>
#include <iostream>
#include <iterator>

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;
using Entries = std::vector<std::pair<std::string, std::string> >;

static void require(const bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

static std::string utf8(const fs::path &path) {
    auto s = path.u8string();
    return {s.begin(), s.end()};
}

static void write(const fs::path &path, const Bytes &data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size()));
    require(static_cast<bool>(out), "write fixture");
}

static Bytes read(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static std::string transform(const std::string_view text) {
    std::string result(text);
    size_t pos = 0;
    while ((pos = result.find("old", pos)) != std::string::npos) {
        result.replace(pos, 3, "new");
        pos += 3;
    }
    return result;
}

// Internal access tests the validation/replacement boundary without a public
// fault-injection API or relying on filesystem timing.
struct OfficeConverterTestAccess {
    static Bytes make(const Entries &entries, const zip_int32_t compression = ZIP_CM_STORE,
                      const bool metadata = false) {
        OfficeConverter::Error error;
        const OfficeConverter::Source source(zip_source_buffer_create(nullptr, 0, 0, &error.value));
        zip_source_keep(source.get());
        OfficeConverter::Source owned(source.get());
        auto archive = OfficeConverter::openSource(owned, ZIP_CREATE | ZIP_TRUNCATE, error);
        for (const auto &[name, data]: entries) {
            OfficeConverter::addBuffer(archive.get(), name, data);
            require(zip_set_file_compression(archive.get(), zip_get_num_entries(archive.get(), 0) - 1,
                                             compression, compression == ZIP_CM_DEFLATE ? 9 : 0) == 0,
                    "fixture compression");
            if (metadata) {
                const auto index = static_cast<zip_uint64_t>(zip_get_num_entries(archive.get(), 0) - 1);
                require(zip_file_set_mtime(archive.get(), index, 1700000000, 0) == 0, "fixture mtime");
                require(zip_file_set_external_attributes(archive.get(), index, 0, ZIP_OPSYS_UNIX,
                                                         0100644u << 16) == 0,
                        "fixture attributes");
                require(zip_file_set_comment(archive.get(), index, "file comment", 12, 0) == 0,
                        "fixture comment");
            }
        }
        if (metadata)
            require(zip_set_archive_comment(archive.get(), "archive comment", 15) == 0, "archive comment");
        OfficeConverter::finish(archive);
        require(zip_source_open(source.get()) == 0, "fixture source");
        Bytes result;
        std::array<uint8_t, 4096> buffer{};
        for (;;) {
            const auto n = zip_source_read(source.get(), buffer.data(), buffer.size());
            require(n >= 0, "fixture read");
            if (!n)
                break;
            result.insert(result.end(), buffer.begin(), buffer.begin() + n);
        }
        zip_source_close(source.get());
        return result;
    }

    static Entries unpack(const Bytes &bytes, const bool epub = false) {
        OfficeConverter::Error error;
        OfficeConverter::Source source(zip_source_buffer_create(bytes.data(), bytes.size(), 0, &error.value));
        const auto archive = OfficeConverter::openSource(source, ZIP_RDONLY | ZIP_CHECKCONS, error);
        Entries result;
        for (zip_int64_t i = 0; i < zip_get_num_entries(archive.get(), 0); ++i)
            result.emplace_back(zip_get_name(archive.get(), i, 0),
                                OfficeConverter::readEntry(archive.get(), i));
        if (epub) {
            zip_stat_t stat{};
            require(zip_stat_index(archive.get(), 0, 0, &stat) == 0, "mimetype stat");
            require(result[0].first == "mimetype" && stat.comp_method == ZIP_CM_STORE, "EPUB order/storage");
        }
        return result;
    }

    struct RawEntry {
        std::string compressed;
        zip_uint16_t method;
        zip_uint64_t size;
        zip_uint32_t crc;
        time_t mtime;
        zip_uint8_t os;
        zip_uint32_t attributes;
        std::string comment;
        std::string archiveComment;

        bool operator==(const RawEntry &) const = default;
    };

    static RawEntry rawEntry(const Bytes &bytes, const char *name) {
        OfficeConverter::Error error;
        OfficeConverter::Source source(zip_source_buffer_create(bytes.data(), bytes.size(), 0, &error.value));
        const auto archive = OfficeConverter::openSource(source, ZIP_RDONLY | ZIP_CHECKCONS, error);
        const auto index = zip_name_locate(archive.get(), name, 0);
        require(index >= 0, "raw entry exists");
        zip_stat_t stat{};
        require(zip_stat_index(archive.get(), index, 0, &stat) == 0, "raw stat");
        RawEntry result{};
        result.method = stat.comp_method;
        result.size = stat.size;
        result.crc = stat.crc;
        result.mtime = stat.mtime;
        require(zip_file_get_external_attributes(archive.get(), index, 0, &result.os, &result.attributes) ==
                0,
                "raw attributes");
        zip_uint32_t length = 0;
        if (const auto comment = zip_file_get_comment(archive.get(), index, &length, 0))
            result.comment.assign(comment, length);
        int archiveLength = 0;
        if (const auto archiveComment = zip_get_archive_comment(archive.get(), &archiveLength, 0))
            result.archiveComment.assign(archiveComment, archiveLength);
        const OfficeConverter::File file(zip_fopen_index(archive.get(), index, ZIP_FL_COMPRESSED));
        require(static_cast<bool>(file), "raw open");
        std::array<char, 8192> buffer{};
        for (;;) {
            const auto n = zip_fread(file.get(), buffer.data(), buffer.size());
            require(n >= 0, "raw read");
            if (!n)
                break;
            result.compressed.append(buffer.data(), static_cast<size_t>(n));
        }
        require(result.compressed.size() == stat.comp_size, "raw compressed size");
        return result;
    }

    static void checkLegacyRawSource(const Bytes &bytes, const char *name) {
        OfficeConverter::Error error;
        OfficeConverter::Source input(zip_source_buffer_create(bytes.data(), bytes.size(), 0, &error.value));
        const auto archive = OfficeConverter::openSource(input, ZIP_RDONLY | ZIP_CHECKCONS, error);
        const auto index = zip_name_locate(archive.get(), name, 0);
        require(index >= 0, "legacy entry exists");
        // Exercise the still-exported legacy function on the installed libzip.
        // The old-version build branch uses this exact argument combination.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        OfficeConverter::Source source(zip_source_zip(archive.get(), archive.get(), index, 0, 0, -1));
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        require(source && zip_source_open(source.get()) == 0, "legacy raw source");
        std::string compressed;
        std::array<char, 8192> buffer{};
        for (;;) {
            const auto n = zip_source_read(source.get(), buffer.data(), buffer.size());
            require(n >= 0, "legacy source read");
            if (!n)
                break;
            compressed.append(buffer.data(), static_cast<size_t>(n));
        }
        zip_source_close(source.get());
        require(compressed == rawEntry(bytes, name).compressed, "legacy full-entry compressed copy");
    }

    static void invalidValidation(const fs::path &destination) {
        OfficeConverter::TempOutput temp(destination);
        const Bytes invalid{'b', 'a', 'd'};
        write(temp.path, invalid);
        const auto archive = OfficeConverter::openFile(temp.path, ZIP_RDONLY | ZIP_CHECKCONS);
        OfficeConverter::validate(archive.get());
        temp.replace(destination);
    }
};

using Access = OfficeConverterTestAccess;

static size_t fileCount(const fs::path &path) {
    return static_cast<size_t>(std::distance(fs::directory_iterator(path), fs::directory_iterator{}));
}
