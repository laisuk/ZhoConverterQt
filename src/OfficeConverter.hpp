#pragma once

#include "OpenccFmmsegHelper.hpp"
#include <zip.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <map>
#include <string>
#include <vector>
#include <sstream>
// #include <ctime>
#include <algorithm>
#include <random>
#include <system_error>

namespace fs = std::filesystem;

// Produce a stable ZIP entry name from an absolute file path and absolute base dir:
// - never throws (uses error_code overloads)
// - falls back to lexical_relative when roots differ (e.g., R:\temp vs C:\...)
// - guarantees forward slashes + UTF-8
static inline std::string make_zip_entry(const fs::path& full, const fs::path& baseAbs)
{
    std::error_code ec;
    fs::path rel = fs::relative(full, baseAbs, ec);

    if (ec || rel.empty() || rel.is_absolute())
    {
        ec.clear();
        fs::path lex = full.lexically_relative(baseAbs);
        if (!lex.empty()) rel = std::move(lex);
        else rel = full.filename(); // last resort: keep file name
    }
    return rel.generic_u8string(); // UTF-8 + '/'
}

// Return an absolute, stable version of a path without throwing.
static inline fs::path stable_abs(const fs::path& p)
{
    std::error_code ec;
    fs::path abs = fs::weakly_canonical(p, ec);
    if (ec)
    {
        ec.clear();
        abs = fs::absolute(p, ec);
    }
    return abs;
}

class OfficeConverter
{
public:
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
        // Normalize base to a stable absolute path (handles \\?\ prefixes, junctions, etc.)
        tempDir = stable_abs(tempDir);
        fs::create_directories(tempDir);

        std::ostringstream debugStream;

        int err = 0;
        zip_t* archive = zip_open(inputPath.c_str(), 0, &err);
        if (!archive) return {false, "❌ Failed to open ZIP archive."};

        zip_int64_t num_entries = zip_get_num_entries(archive, 0);
        for (zip_uint64_t i = 0; i < num_entries; ++i)
        {
            const char* name = zip_get_name(archive, i, 0);
            if (zip_file_t* file = zip_fopen_index(archive, i, 0))
            {
                fs::path filePath = tempDir / fs::path(name);
                fs::create_directories(filePath.parent_path());
                std::ofstream out(filePath, std::ios::binary);
                char buffer[4096];
                zip_int64_t bytes_read;
                while ((bytes_read = zip_fread(file, buffer, sizeof(buffer))) > 0)
                {
                    out.write(buffer, bytes_read);
                }
                out.close();
                zip_fclose(file);
            }
        }
        zip_close(archive);

        std::vector<fs::path> targetXmls = getTargetXmlPaths(format, tempDir);
        size_t convertedCount = 0;

        for (auto& file : targetXmls)
        {
            if (!fs::exists(file))
            {
                debugStream << "⚠️ File does not exist: " << file << "\n";
                continue;
            }

            debugStream << "🔄 Converting file: " << file << "\n";
            std::ifstream in(file);
            std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();

            std::map<std::string, std::string> fontMap;
            if (keepFont)
            {
                maskFont(xml, format, fontMap);
                debugStream << "🎨 Font masked with " << fontMap.size() << " markers.\n";
            }

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

            ++convertedCount;
        }

        if (convertedCount == 0)
        {
            debugStream << "❌ No fragments were converted. Nothing changed.\n";
            fs::remove_all(tempDir);
            return {false, debugStream.str()};
        }

        if (fs::exists(outputPath)) fs::remove(outputPath);

        zip_t* zipOut = zip_open(outputPath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
        if (!zipOut)
        {
            fs::remove_all(tempDir);
            return {false, "❌ Failed to create output ZIP."};
        }

        std::vector<std::string> filenames;
        std::vector<std::vector<char>> fileBuffers;

        // for (const auto &p: fs::recursive_directory_iterator(tempDir)) {
        //     if (!fs::is_regular_file(p)) continue;
        //
        //     std::ifstream in(p.path(), std::ios::binary);
        //     std::vector<char> buffer((std::istreambuf_iterator<char>(in)), {});
        //     in.close();
        //
        //     std::string relative = fs::relative(p.path(), tempDir).generic_string();
        //     filenames.push_back(relative);
        //     fileBuffers.push_back(std::move(buffer));
        // }
        {
            std::error_code ec;
            // Iterate without throwing; skip unreadable nodes just in case.
            fs::recursive_directory_iterator it(
                                                 tempDir,
                                                 fs::directory_options::skip_permission_denied,
                                                 ec
                                             ), end;

            for (; it != end; it.increment(ec))
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

                // Read payload (brace-init to avoid most-vexing-parse)
                std::ifstream in(full, std::ios::binary);
                if (!in) { continue; }
                std::vector<char> buf{
                    std::istreambuf_iterator<char>(in),
                    std::istreambuf_iterator<char>()
                };
                // Compute a robust, portable ZIP entry path
                const std::string entry = make_zip_entry(full, tempDir);

                filenames.push_back(entry);
                fileBuffers.push_back(std::move(buf));
            }
        }

        // after collecting filenames + fileBuffers
        if (format == "epub")
        {
            for (size_t i = 0; i < filenames.size(); ++i)
            {
                // normalize to exactly "mimetype"
                if (filenames[i] == "mimetype" || filenames[i] == "./mimetype" || filenames[i] == "/mimetype")
                {
                    if (i != 0)
                    {
                        std::swap(filenames[0], filenames[i]);
                        std::swap(fileBuffers[0], fileBuffers[i]);
                    }
                    // also normalize the name to exactly "mimetype"
                    filenames[0] = "mimetype";
                    break;
                }
            }
        }

        for (size_t i = 0; i < filenames.size(); ++i)
        {
            zip_source_t* source = zip_source_buffer(zipOut, fileBuffers[i].data(), fileBuffers[i].size(), 0);
            if (!source || zip_file_add(zipOut, filenames[i].c_str(), source, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) <
                0)
            {
                if (source) zip_source_free(source);
                zip_discard(zipOut);
                fs::remove_all(tempDir);
                return {false, "❌ Failed to add file to ZIP: " + filenames[i]};
            }
        }

        zip_close(zipOut);
        fs::remove_all(tempDir);

        // debugStream << "✅ Converted " << convertedCount << " fragment(s).\n";
        return {true, "✅ Conversion completed.\n✅ Converted " + std::to_string(convertedCount) + " fragment(s).\n"};
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
