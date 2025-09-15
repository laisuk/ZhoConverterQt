#pragma once

#include "OpenccFmmsegHelper.hpp"
#include "ZipPathUtils.hpp"
// #include "mz_strm.h"
#include <minizip-ng/mz.h>
#include <minizip-ng/mz_zip.h>
#include <minizip-ng/mz_zip_rw.h>

#include <filesystem>
#include <system_error>
#include <iterator>
#include <fstream>
#include <regex>
#include <map>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <random>
#include <unordered_set>

namespace fs = std::filesystem;

class OfficeConverterMinizip
{
public:
    static inline const std::unordered_set<std::string> OFFICE_EXTENSIONS = {
        "docx", "xlsx", "pptx", "odt", "ods", "odp", "epub"
    };

    struct Result
    {
        bool success;
        std::string message;
    };

    static Result Convert(const std::string& inputPath,
                          const std::string& outputPath,
                          const std::string& format,
                          OpenccFmmsegHelper& helper,
                          const std::string& config,
                          bool punctuation,
                          bool keepFont = false)
    {
        fs::path tempDir = fs::temp_directory_path() / ("office_tmp_" + std::to_string(std::random_device{}()));
        tempDir = office::zip::stable_abs(tempDir);
        fs::create_directories(tempDir);

        struct TempDirGuard
        {
            fs::path p;

            ~TempDirGuard()
            {
                std::error_code ec;
                fs::remove_all(p, ec);
            }
        } _temp_guard{tempDir};

        std::ostringstream debugStream;

        // --- Extract ZIP ---
        void* reader = mz_zip_reader_create();
        std::string normalizedInput = inputPath;
        std::replace(normalizedInput.begin(), normalizedInput.end(), '\\', '/');

        if (mz_zip_reader_open_file(reader, normalizedInput.c_str()) != MZ_OK)
        {
            return {false, "❌ Failed to open ZIP archive."};
        }

        debugStream << "🗂 ZIP Entries:\n";
        if (mz_zip_reader_goto_first_entry(reader) == MZ_OK)
        {
            do
            {
                mz_zip_file* file_info = nullptr;
                mz_zip_reader_entry_get_info(reader, &file_info);
                if (!file_info || !file_info->filename) continue;

                debugStream << "  -> " << file_info->filename << "\n";

                // Directory entry? just create the dir and continue
                if (mz_zip_reader_entry_is_dir(reader) == MZ_OK)
                {
                    std::error_code ec;
                    fs::create_directories((tempDir / file_info->filename).parent_path(), ec);
                    fs::create_directories(tempDir / file_info->filename, ec);
                    continue;
                }

                const fs::path outPath = tempDir / file_info->filename;
                fs::create_directories(outPath.parent_path());

                // One-shot save (no need to keep entry open yourself)
                if (int rc = mz_zip_reader_entry_save_file(reader, outPath.string().c_str()); rc != MZ_OK)
                {
                    // As a fallback, try opening/closing explicitly once
                    if (mz_zip_reader_entry_open(reader) == MZ_OK)
                    {
                        rc = mz_zip_reader_entry_save_file(reader, outPath.string().c_str());
                        mz_zip_reader_entry_close(reader);
                    }
                    if (rc != MZ_OK)
                    {
                        debugStream << "    [save_file failed rc=" << rc << "]\n";
                    }
                }
            }
            while (mz_zip_reader_goto_next_entry(reader) == MZ_OK);
        }

        // --- Conversion ---
        std::vector<fs::path> targetXmls = getTargetXmlPaths(format, tempDir);
        int convertedCount = 0;

        for (auto& file : targetXmls)
        {
            if (!fs::exists(file)) continue;
            std::ifstream in(file);
            std::string xml{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
            in.close();

            std::map<std::string, std::string> fontMap;
            if (keepFont) maskFont(xml, format, fontMap);

            std::string converted = helper.convert(xml, config, punctuation);

            if (keepFont)
            {
                for (auto& [marker, original] : fontMap)
                {
                    size_t pos;
                    while ((pos = converted.find(marker)) != std::string::npos)
                    {
                        converted.replace(pos, marker.length(), original);
                    }
                }
            }

            std::ofstream out(file);
            out << converted;
            out.close();
            convertedCount++;
        }

        // --- Write ZIP ---
        void* writer = mz_zip_writer_create();
        bool ok = true;

        // make sure output directory exists
        {
            std::error_code ec;
            fs::create_directories(fs::path(outputPath).parent_path(), ec);
            // optional: remove existing file
            fs::remove(outputPath, ec);
        }

        if (int rc_open = mz_zip_writer_open_file(writer, outputPath.c_str(), 0, 0); rc_open != MZ_OK)
        {
            ok = false;
            debugStream << "minizip: open_file failed rc=" << rc_open << "\n";
        }
        else
        {
            // defaults
            mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_DEFLATE);
            mz_zip_writer_set_compress_level(writer, MZ_COMPRESS_LEVEL_DEFAULT);

            // EPUB: write mimetype first, stored (no compression)
            if (format == "epub")
            {
                if (fs::path mimetypePath = tempDir / "mimetype"; fs::exists(mimetypePath))
                {
                    std::ifstream in(mimetypePath, std::ios::binary);
                    std::vector<char> buf{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};

                    mz_zip_file file_info = {};
                    file_info.filename = "mimetype";
                    file_info.flag |= MZ_ZIP_FLAG_UTF8;
                    file_info.compression_method = MZ_COMPRESS_METHOD_STORE;

                    mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_STORE);
                    mz_zip_writer_set_compress_level(writer, 0);

                    if (int rc = mz_zip_writer_add_buffer(writer, buf.data(), static_cast<int32_t>(buf.size()),
                                                          &file_info); rc != MZ_OK)
                    {
                        ok = false;
                        debugStream << "minizip: add_buffer(mimetype) failed rc=" << rc << "\n";
                    }

                    // restore defaults
                    mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_DEFLATE);
                    mz_zip_writer_set_compress_level(writer, MZ_COMPRESS_LEVEL_DEFAULT);
                }
            }

            // add all other files
            if (ok)
            {
                std::error_code ec;
                for (fs::recursive_directory_iterator
                         it(tempDir, fs::directory_options::skip_permission_denied, ec), end;
                     it != end; it.increment(ec))
                {
                    if (ec)
                    {
                        ec.clear();
                        continue;
                    }

                    const fs::path full = it->path();
                    if (!it->is_regular_file(ec))
                    {
                        ec.clear();
                        continue;
                    }
                    if (format == "epub" && full.filename() == "mimetype") continue;

                    const std::string entry = office::zip::make_zip_entry(full, tempDir);

                    std::ifstream in(full, std::ios::binary);
                    if (!in) continue;
                    std::vector<char> buf{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};

                    mz_zip_file file_info = {};
                    file_info.filename = entry.c_str();
                    file_info.flag |= MZ_ZIP_FLAG_UTF8;
                    file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;

                    mz_zip_writer_set_compress_method(writer, MZ_COMPRESS_METHOD_DEFLATE);
                    mz_zip_writer_set_compress_level(writer, MZ_COMPRESS_LEVEL_DEFAULT);

                    if (int rc = mz_zip_writer_add_buffer(writer, buf.data(), static_cast<int32_t>(buf.size()),
                                                          &file_info); rc != MZ_OK)
                    {
                        ok = false;
                        debugStream << "minizip: add_buffer(" << entry << ") failed rc=" << rc << "\n";
                        break;
                    }
                }
            }
        }

        // close & delete writer
        int rc_close = mz_zip_writer_close(writer);
        mz_zip_writer_delete(&writer);

        // final status
        if (!ok) return {false, debugStream.str()};
        if (rc_close != MZ_OK) return {false, "minizip: close failed rc=" + std::to_string(rc_close)};
        if (convertedCount == 0)
            return {false, debugStream.str()};

        return {true, "✅ Successfully converted " + std::to_string(convertedCount) + " fragment(s)."};
    }

private:
    static std::vector<fs::path> getTargetXmlPaths(const std::string& format, const fs::path& baseDir)
    {
        std::vector<fs::path> result;
        if (format == "docx")
        {
            result.push_back(baseDir / "word" / "document.xml");
        }
        else if (format == "xlsx")
        {
            result.push_back(baseDir / "xl" / "sharedStrings.xml");
        }
        else if (format == "pptx")
        {
            for (auto& p : fs::recursive_directory_iterator(baseDir / "ppt"))
            {
                if (p.path().filename().string().find("slide") != std::string::npos ||
                    p.path().string().find("notesSlide") != std::string::npos)
                {
                    result.push_back(p.path());
                }
            }
        }
        else if (format == "odt" || format == "ods" || format == "odp")
        {
            result.push_back(baseDir / "content.xml");
        }
        else if (format == "epub")
        {
            for (auto& p : fs::recursive_directory_iterator(baseDir))
            {
                if (std::string ext = p.path().extension().string();
                    ext == ".xhtml" || ext == ".html" || ext == ".opf" || ext == ".ncx")
                {
                    result.push_back(p.path());
                }
            }
        }
        return result;
    }

    static void maskFont(std::string& xml, const std::string& format, std::map<std::string, std::string>& fontMap)
    {
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
        while (std::regex_search(begin, end, match, pattern))
        {
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
