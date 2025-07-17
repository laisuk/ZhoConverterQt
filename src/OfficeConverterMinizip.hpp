#pragma once

#include "OpenccFmmsegHelper.hpp"
// #include "mz_strm.h"
#include <minizip/mz.h>
#include <minizip/mz_zip.h>
#include <minizip/mz_zip_rw.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <map>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <random>

namespace fs = std::filesystem;

class OfficeConverterMinizip {
public:
    struct Result {
        bool success;
        std::string message;
    };

    static Result Convert(const std::string &inputPath,
                          const std::string &outputPath,
                          const std::string &format,
                          OpenccFmmsegHelper &helper,
                          const std::string &config,
                          bool punctuation,
                          bool keepFont = false) {
        fs::path tempDir = fs::temp_directory_path() / ("office_tmp_" + std::to_string(std::random_device{}()));

        fs::create_directories(tempDir);
        std::ostringstream debugStream;

        void *reader = mz_zip_reader_create();
        std::string normalizedInput = inputPath;
        std::replace(normalizedInput.begin(), normalizedInput.end(), '\\', '/');

        if (mz_zip_reader_open_file(reader, normalizedInput.c_str()) != MZ_OK) {
            return {false, "❌ Failed to open ZIP archive."};
        }

        debugStream << "🗂 ZIP Entries:\n";
        if (mz_zip_reader_goto_first_entry(reader) == MZ_OK) {
            do {
                mz_zip_file *file_info = nullptr;
                mz_zip_reader_entry_get_info(reader, &file_info);
                if (file_info && file_info->filename) {
                    debugStream << "  -> " << file_info->filename << "\n";

                    std::string outPath = (tempDir / file_info->filename).string();
                    fs::create_directories(fs::path(outPath).parent_path());

                    if (mz_zip_reader_entry_open(reader) != MZ_OK) continue;
                    mz_zip_reader_entry_save_file(reader, outPath.c_str());
                    mz_zip_reader_entry_close(reader);
                }
            } while (mz_zip_reader_goto_next_entry(reader) == MZ_OK);
        }

        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);

        std::vector<fs::path> targetXmls = getTargetXmlPaths(format, tempDir);

        debugStream << "📂 Extracted files in temp dir:\n";
        for (const auto &p: fs::recursive_directory_iterator(tempDir)) {
            debugStream << "  -> " << p.path() << "\n";
        }

        int convertedCount = 0;
        for (auto &file: targetXmls) {
            if (!fs::exists(file)) continue;

            std::ifstream in(file);
            std::string xml((std::istreambuf_iterator<char>(in)), {});
            in.close();

            std::map<std::string, std::string> fontMap;
            if (keepFont) maskFont(xml, format, fontMap);

            std::string converted = helper.convert(xml, config, punctuation);

            if (keepFont) {
                for (auto &[marker, original]: fontMap) {
                    size_t pos;
                    while ((pos = converted.find(marker)) != std::string::npos) {
                        converted.replace(pos, marker.length(), original);
                    }
                }
            }

            std::ofstream out(file);
            out << converted;
            out.close();

            convertedCount++;
        }

        void *writer = mz_zip_writer_create();
        mz_zip_writer_open_file(writer, outputPath.c_str(), 0, 0);

        if (format == "epub") {
            if (fs::path mimetypePath = tempDir / "mimetype"; fs::exists(mimetypePath)) {
                std::ifstream in(mimetypePath, std::ios::binary);
                std::vector<char> buf((std::istreambuf_iterator<char>(in)), {});

                mz_zip_file file_info = {};
                file_info.filename = "mimetype";
                file_info.compression_method = MZ_COMPRESS_METHOD_STORE;
                // Set compression level to 6
                mz_zip_writer_set_compress_level(writer, 0);

                mz_zip_writer_add_buffer(writer, buf.data(), static_cast<int32_t>(buf.size()), &file_info);
            }
        }

        for (const auto &p: fs::recursive_directory_iterator(tempDir)) {
            if (!fs::is_regular_file(p) || p.path().filename() == "mimetype") continue;

            std::string relative = fs::relative(p.path(), tempDir).string();
            std::replace(relative.begin(), relative.end(), '\\', '/');

            std::ifstream in(p.path(), std::ios::binary);
            std::vector<char> buf((std::istreambuf_iterator<char>(in)), {});

            mz_zip_file file_info = {};
            file_info.filename = relative.c_str();
            file_info.compression_method = MZ_COMPRESS_METHOD_DEFLATE;

            // Set compression level to 6
            mz_zip_writer_set_compress_level(writer, MZ_COMPRESS_LEVEL_DEFAULT);
            mz_zip_writer_add_buffer(writer, buf.data(), static_cast<int32_t>(buf.size()), &file_info);
        }

        mz_zip_writer_close(writer);
        mz_zip_writer_delete(&writer);
        fs::remove_all(tempDir);

        if (convertedCount == 0)
            return {false, debugStream.str()};

        return {true, "✅ Successfully converted " + std::to_string(convertedCount) + " fragment(s)."};
    }

private:
    static std::vector<fs::path> getTargetXmlPaths(const std::string &format, const fs::path &baseDir) {
        std::vector<fs::path> result;
        if (format == "docx") {
            result.push_back(baseDir / "word" / "document.xml");
        } else if (format == "xlsx") {
            result.push_back(baseDir / "xl" / "sharedStrings.xml");
        } else if (format == "pptx") {
            for (auto &p: fs::recursive_directory_iterator(baseDir / "ppt")) {
                if (p.path().filename().string().find("slide") != std::string::npos ||
                    p.path().string().find("notesSlide") != std::string::npos) {
                    result.push_back(p.path());
                }
            }
        } else if (format == "odt" || format == "ods" || format == "odp") {
            result.push_back(baseDir / "content.xml");
        } else if (format == "epub") {
            for (auto &p: fs::recursive_directory_iterator(baseDir)) {
                if (std::string ext = p.path().extension().string();
                    ext == ".xhtml" || ext == ".opf" || ext == ".ncx") {
                    result.push_back(p.path());
                }
            }
        }
        return result;
    }

    static void maskFont(std::string &xml, const std::string &format, std::map<std::string, std::string> &fontMap) {
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
