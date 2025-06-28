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
#include <ctime>
#include <algorithm>

namespace fs = std::filesystem;

class OfficeDocConverter {
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
        fs::path tempDir = fs::temp_directory_path() / ("office_tmp_" + std::to_string(std::rand()));
        fs::create_directories(tempDir);

        std::ostringstream debugStream;

        int err = 0;
        zip_t *archive = zip_open(inputPath.c_str(), 0, &err);
        if (!archive) return {false, "❌ Failed to open ZIP archive."};

        zip_int64_t num_entries = zip_get_num_entries(archive, 0);
        for (zip_uint64_t i = 0; i < num_entries; ++i) {
            const char *name = zip_get_name(archive, i, 0);
            zip_file_t *file = zip_fopen_index(archive, i, 0);
            if (file) {
                fs::path filePath = tempDir / fs::path(name);
                fs::create_directories(filePath.parent_path());
                std::ofstream out(filePath, std::ios::binary);
                char buffer[4096];
                zip_int64_t bytes_read;
                while ((bytes_read = zip_fread(file, buffer, sizeof(buffer))) > 0) {
                    out.write(buffer, bytes_read);
                }
                out.close();
                zip_fclose(file);
            }
        }
        zip_close(archive);

        std::vector<fs::path> targetXmls = getTargetXmlPaths(format, tempDir);

        for (auto &file: targetXmls) {
            if (!fs::exists(file)) continue;

            std::ifstream in(file);
            std::string xml((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();

            std::map<std::string, std::string> fontMap;
            if (keepFont) {
                maskFont(xml, format, fontMap);
            }

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
        }

        if (fs::exists(outputPath)) fs::remove(outputPath);

        zip_t *zipOut = zip_open(outputPath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
        if (!zipOut) {
            fs::remove_all(tempDir);
            return {false, "❌ Failed to create output ZIP."};
        }

        std::vector<std::string> filenames;
        std::vector<std::vector<char> > fileBuffers;

        for (const auto &p: fs::recursive_directory_iterator(tempDir)) {
            if (!fs::is_regular_file(p)) continue;

            std::ifstream in(p.path(), std::ios::binary);
            std::vector<char> buffer((std::istreambuf_iterator<char>(in)), {});
            in.close();

            std::string relative = fs::relative(p.path(), tempDir).generic_string();
            filenames.push_back(relative);
            fileBuffers.push_back(std::move(buffer));
        }

        for (size_t i = 0; i < filenames.size(); ++i) {
            zip_source_t *source = zip_source_buffer(zipOut, fileBuffers[i].data(), fileBuffers[i].size(), 0);
            if (!source || zip_file_add(zipOut, filenames[i].c_str(), source, ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) <
                0) {
                if (source) zip_source_free(source);
                zip_discard(zipOut);
                fs::remove_all(tempDir);
                return {false, "❌ Failed to add file to ZIP: " + filenames[i]};
            }
        }

        zip_close(zipOut);
        fs::remove_all(tempDir);

        return {true, "✅ Conversion completed."};
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
                std::string ext = p.path().extension().string();
                if (ext == ".xhtml" || ext == ".opf" || ext == ".ncx") {
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
            pattern = std::regex(R"((font-family\s*:\s*)([^;"']+))");
        else
            return;

        std::smatch match;
        size_t counter = 0;
        std::string result;
        auto begin = xml.cbegin();
        auto end = xml.cend();
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
