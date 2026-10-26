#include "office_test_support.hpp"

int main() {
    try {
        fs::path directory;
        directory = fs::current_path() / ("office-tests-" + std::to_string(std::random_device{}()));
        require(fs::create_directory(directory), "test directory");
        struct Cleanup {
            fs::path path;

            ~Cleanup() {
                std::error_code ec;
                fs::remove_all(path, ec);
            }
        } cleanup{directory};
        const auto input = directory / fs::path(u8"輸入.docx");
        const auto output = directory / fs::path(u8"輸出.docx");

        // Seven formats, shared implementation, target selection and unchanged binary/directory entries.
        const std::vector<std::pair<std::string, std::string> > formats{
            {"docx", "word/document.xml"},
            {"xlsx", "xl/sharedStrings.xml"},
            {"pptx", "ppt/slides/slide1.xml"},
            {"odt", "content.xml"},
            {"ods", "content.xml"},
            {"odp", "content.xml"},
            {"epub", "OEBPS/book.xhtml"}
        };
        for (const auto &[format, target]: formats) {
            Entries entries{
                {target, "<t>old</t>"},
                {"media/", ""},
                {"media/image.bin", std::string("\0old\xff", 5)},
                {"docProps/core.xml", "old"}
            };
            if (format == "epub")
                entries.emplace_back("mimetype", "application/epub+zip");
            const auto bytes = Access::make(entries);
            write(input, bytes);
            const auto memory = OfficeConverter::ConvertBytes(bytes, format, transform);
            require(memory.success, format + ": " + memory.message);
            const auto file = OfficeConverter::Convert(utf8(input), utf8(output), format, transform);
            require(file.success, format + ": " + file.message);
            const auto a = Access::unpack(memory.outputBytes, format == "epub");
            const auto b = Access::unpack(read(output), format == "epub");
            require(a == b, "memory/file equivalence");
            const auto lookup = [&](const std::string &name) {
                return std::ranges::find_if(a, [&](const auto &e) { return e.first == name; })
                        ->second;
            };
            require(lookup(target) == "<t>new</t>", "target conversion");
            require(lookup("docProps/core.xml") == "old", "unrelated metadata");
            require(lookup("media/image.bin") == entries[2].second, "binary preservation");
        }

        // Reject path aliases before invoking the text callback, through both
        // APIs. Directory terminators are allowed; internal empty/"." parts are not.
        const auto checkRejectedArchive = [&](const Bytes &bytes, const std::string &format) {
            int calls = 0;
            const TextConverter callback = [&](std::string_view text) {
                ++calls;
                return transform(text);
            };
            const auto memory = OfficeConverter::ConvertBytes(bytes, format, callback);
            require(!memory.success && calls == 0, "invalid paths rejected before memory callback");
            write(input, bytes);
            const Bytes sentinel{'k', 'e', 'e', 'p'};
            write(output, sentinel);
            const auto file = OfficeConverter::Convert(utf8(input), utf8(output), format, callback);
            require(!file.success && calls == 0 && read(output) == sentinel && fileCount(directory) == 2,
                    "invalid paths preserve destination/cleanup");
        };
        const auto checkRejectedPaths = [&](const Entries &entries, const std::string &format) {
            checkRejectedArchive(Access::make(entries), format);
        };
        // libzip's writer rejects exact duplicate names, so construct one by
        // replacing equal-length names in the local and central directory.
        auto exactDuplicate = Access::make({{"word/document.xml", "old"}, {"word/documenx.xml", "old"}});
        const std::string originalName = "word/documenx.xml";
        const std::string duplicateName = "word/document.xml";
        auto position = exactDuplicate.begin();
        int replacements = 0;
        while ((position = std::search(position, exactDuplicate.end(), originalName.begin(),
                                       originalName.end())) != exactDuplicate.end()) {
            std::ranges::copy(duplicateName, position);
            position += static_cast<Bytes::difference_type>(duplicateName.size());
            ++replacements;
        }
        require(replacements == 2, "duplicate fixture local/central names");
        checkRejectedArchive(exactDuplicate, "docx");
        for (const auto *name: {
                 "word/./document.xml", "word//document.xml", "word/../document.xml",
                 "./word/document.xml", "/word/document.xml", "C:word/document.xml",
                 "C:/word/document.xml", R"(\\server\share\file)", "word//"
             }) {
            checkRejectedPaths({{"word/document.xml", "old"}, {name, "old"}}, "docx");
        }
        checkRejectedPaths({{"word/document.xml", "old"}, {R"(word\document.xml)", "old"}}, "docx");
        checkRejectedPaths({{"word/document.xml", "old"}, {"media", "old"}, {"media/", ""}}, "docx");
        const auto epubBase = Entries{{"book.xhtml", "old"}, {"mimetype", "application/epub+zip"}};
        for (const auto *name: {"./mimetype", "/mimetype", "dir/../mimetype", "mimetype/", "dir//data"}) {
            auto entries = epubBase;
            entries.emplace_back(name, "application/epub+zip");
            checkRejectedPaths(entries, "epub");
        }
        const auto legitimate = Access::make({
            {R"(word\document.xml)", "old"},
            {"media/文件 v1..png", "old"},
            {"media/.hidden", "old"},
            {"media/", ""}
        });
        auto legitimateResult = OfficeConverter::ConvertBytes(legitimate, "docx", transform);
        require(legitimateResult.success, "Unicode/spaces/dots/backslash separator remain valid");
        require(Access::unpack(legitimateResult.outputBytes)[0].first == "word/document.xml",
                "separator normalization");

        // Verify actual compressed bytes as well as payload/method/metadata;
        // comparing only decompressed bytes would miss accidental recompression.
        std::string binary;
        for (unsigned i = 0; i < 256; ++i)
            binary.push_back(static_cast<char>(i));
        const auto resources = Entries{{"word/document.xml", "old"}, {"media/resource.bin", binary + binary}};
        for (const auto method: {ZIP_CM_STORE, ZIP_CM_DEFLATE}) {
            const auto bytes = Access::make(resources, method, true);
            auto memory = OfficeConverter::ConvertBytes(bytes, "docx", transform);
            require(memory.success, memory.message);
            write(input, bytes);
            auto file = OfficeConverter::Convert(utf8(input), utf8(output), "docx", transform);
            require(file.success, file.message);
            const auto original = Access::rawEntry(bytes, "media/resource.bin");
            Access::checkLegacyRawSource(bytes, "media/resource.bin");
            require(original.method == method &&
                    Access::rawEntry(memory.outputBytes, "media/resource.bin") == original &&
                    Access::rawEntry(read(output), "media/resource.bin") == original,
                    "raw-copy bytes and metadata");
            require(Access::unpack(memory.outputBytes)[1].second == binary + binary,
                    "compressed binary payload");
        }

        // An input cap is opt-in, applies per target, and is checked before the
        // callback and before allocating from a claimed uncompressed size.
        const auto limitedBytes = Access::make({{"word/document.xml", "old"}}, ZIP_CM_DEFLATE);
        int limitCalls = 0;
        const TextConverter limitedCallback = [&](const std::string_view text) {
            ++limitCalls;
            return transform(text);
        };
        const auto limited = OfficeConverter::ConvertBytes(limitedBytes, "docx", limitedCallback, false, 2);
        require(!limited.success && limited.message.find("maximum target-entry size") != std::string::npos &&
                limitCalls == 0,
                "declared size limit");
        require(OfficeConverter::ConvertBytes(limitedBytes, "docx", limitedCallback, false, 3).success &&
                limitCalls == 1,
                "exact size limit");
        write(input, limitedBytes);
        const Bytes limitSentinel{'k', 'e', 'e', 'p'};
        write(output, limitSentinel);
        const auto limitedFile =
                OfficeConverter::Convert(utf8(input), utf8(output), "docx", limitedCallback, false, 2);
        require(!limitedFile.success && limitCalls == 1 && read(output) == limitSentinel &&
                fileCount(directory) == 2,
                "file size limit preserves destination");
        require(OfficeConverter::ConvertBytes(Access::make({{"word/document.xml", ""}}), "docx", transform,
                                              false, 0)
                .success,
                "zero size cap permits empty target");
        const auto nonTargetLarge = Access::make(
            {{"word/document.xml", "old"}, {"media/resource", std::string(40000, 'x')}}, ZIP_CM_DEFLATE);
        require(OfficeConverter::ConvertBytes(nonTargetLarge, "docx", transform, false, 3).success,
                "target cap does not limit binary resources");

        // Forge both local and central size fields while retaining a tiny
        // compressed payload. No multi-gigabyte allocation is needed to test it.
        auto oversized = limitedBytes;
        for (size_t pos = 0; pos + 28 < oversized.size(); ++pos) {
            if (oversized[pos] != 'P' || oversized[pos + 1] != 'K')
                continue;
            size_t field = 0;
            if (oversized[pos + 2] == 3 && oversized[pos + 3] == 4)
                field = pos + 22;
            if (oversized[pos + 2] == 1 && oversized[pos + 3] == 2)
                field = pos + 24;
            if (!field)
                continue;
            for (size_t i = 0; i < 4; ++i)
                oversized[field + i] = i == 3 ? 0x40 : 0;
        }
        limitCalls = 0;
        require(!OfficeConverter::ConvertBytes(oversized, "docx", limitedCallback, false, 1024).success &&
                limitCalls == 0,
                "oversized declared uncompressed size");

        auto undersized = limitedBytes;
        for (size_t pos = 0; pos + 28 < undersized.size(); ++pos) {
            if (undersized[pos] != 'P' || undersized[pos + 1] != 'K')
                continue;
            size_t field = 0;
            if (undersized[pos + 2] == 3 && undersized[pos + 3] == 4)
                field = pos + 22;
            if (undersized[pos + 2] == 1 && undersized[pos + 3] == 2)
                field = pos + 24;
            if (!field)
                continue;
            for (size_t i = 0; i < 4; ++i)
                undersized[field + i] = i == 0 ? 1 : 0;
        }
        require(!OfficeConverter::ConvertBytes(undersized, "docx", limitedCallback, false, 1024).success &&
                limitCalls == 0,
                "understated uncompressed size rejected before callback");
        const auto oversizedMime = OfficeConverter::ConvertBytes(
            Access::make({{"book.xhtml", "old"}, {"mimetype", std::string(100, 'x')}}), "epub",
            limitedCallback);
        require(!oversizedMime.success && limitCalls == 0, "EPUB mimetype read is independently bounded");

        // Prefixes found anywhere in original text or font values cannot become
        // placeholders, and multiple fonts still restore independently.
        const std::string markerXml = R"(<w:rFonts w:ascii="old__F_O_N_T_0__" w:eastAsia="other old"/>)"
                "<w:t>old __F_O_N_T_0__ __F_O_N_T__1__ __F_O_N_T___2__</w:t>";
        const std::string markerExpected = R"(<w:rFonts w:ascii="old__F_O_N_T_0__" w:eastAsia="other old"/>)"
                "<w:t>new __F_O_N_T_0__ __F_O_N_T__1__ __F_O_N_T___2__</w:t>";
        const auto markerBytes = Access::make({{"word/document.xml", markerXml}});
        auto markerResult = OfficeConverter::ConvertBytes(markerBytes, "docx", transform, true);
        require(markerResult.success && Access::unpack(markerResult.outputBytes)[0].second == markerExpected,
                "literal font-marker collisions");
        write(input, markerBytes);
        auto markerFile = OfficeConverter::Convert(utf8(input), utf8(output), "docx", transform, true);
        require(markerFile.success && Access::unpack(read(output))[0].second == markerExpected,
                "file font-marker collisions");

        // Composition receives content and executes normalization -> engine -> postprocess.
        std::string stages;
        TextConverter pipeline = [&](std::string_view text) {
            stages += 'N';
            const auto normalized = std::string(text) + "N";
            stages += 'C';
            const auto converted = transform(normalized);
            stages += 'D';
            return converted + "D";
        };
        auto composed =
                OfficeConverter::ConvertBytes(Access::make({{"word/document.xml", "old"}}), "DOCX", pipeline);
        require(composed.success && stages == "NCD" &&
                Access::unpack(composed.outputBytes)[0].second == "newND",
                "callback composition");

        const std::string sheet = "<worksheet><c t='inlineStr'><is><t>old</t><t></t><t/></is></c>"
                "<c><f>old+1</f><v>old</v></c><c t='s'><v>old</v></c></worksheet>";
        int callbacks = 0;
        auto xlsx = OfficeConverter::ConvertBytes(Access::make({{"XL\\WORKSHEETS\\sheet1.XML", sheet}}),
                                                  "xlsx", [&](const std::string_view text) {
                                                      ++callbacks;
                                                      return transform(text);
                                                  });
        require(xlsx.success && callbacks == 1, "XLSX empty text callback treatment");
        auto expected = sheet;
        expected.replace(expected.find("<t>old"), 6, "<t>new");
        require(Access::unpack(xlsx.outputBytes)[0] ==
                std::pair<std::string, std::string>{"XL/WORKSHEETS/sheet1.XML", expected},
                "XLSX narrow conversion");

        // Multiple transformed ranges share the file spool, including an empty
        // target and entries crossing the fixed-size read buffer.
        const auto many = Access::make({
            {"ppt/slides/slide1.xml", std::string(20000, 'x') + "old"},
            {"ppt/slides/slide2.xml", "old old"},
            {"ppt/slides/slide3.xml", ""}
        });
        write(input, many);
        const auto manyMemory = OfficeConverter::ConvertBytes(many, "pptx", transform);
        const auto manyFile = OfficeConverter::Convert(utf8(input), utf8(output), "pptx", transform);
        require(manyMemory.success && manyFile.success &&
                Access::unpack(manyMemory.outputBytes) == Access::unpack(read(output)),
                "spooled ranges");
        Entries ppt;
        for (const auto *part: {
                 "slides/slide1", "notesSlides/notesSlide1", "slideMasters/slideMaster1",
                 "slideLayouts/slideLayout1", "comments/comment1", "commentAuthors"
             })
            ppt.emplace_back(std::string("PPT/") + part + ".XML", "old");
        ppt.emplace_back("ppt/slides/_rels/slide1.xml.rels", "old");
        auto slides = OfficeConverter::ConvertBytes(Access::make(ppt), "pptx", transform);
        require(slides.success, slides.message);
        auto unpacked = Access::unpack(slides.outputBytes);
        for (size_t i = 0; i < ppt.size(); ++i)
            require(unpacked[i].second == (i + 1 == ppt.size() ? "old" : "new"), "PPTX parts");

        for (const auto &[format, target]: formats) {
            std::string xml = "<t>old</t>";
            if (format == "docx")
                xml += "<w:rFonts w:ascii=\"old\"/>";
            if (format == "xlsx")
                xml += "<font val=\"old\"/>";
            if (format == "pptx")
                xml += "<a:font typeface=\"old\"/>";
            if (format == "odt" || format == "ods" || format == "odp")
                xml += "<x style:font-name='old'/>";
            if (format == "epub")
                xml += "<p style=\"font-family: old;\">old</p>";
            Entries entries{{target, xml}};
            if (format == "epub")
                entries.emplace_back("mimetype", "application/epub+zip");
            auto result = OfficeConverter::ConvertBytes(Access::make(entries), format, transform, true);
            require(result.success, result.message);
            const auto converted = Access::unpack(result.outputBytes, format == "epub");
            const auto found = std::ranges::find_if(converted,
                                                    [&](const auto &e) { return e.first == target; });
            require(found->second.find("<t>new</t>") != std::string::npos &&
                    found->second.find("old") != std::string::npos,
                    "font preservation " + format);
        }

        require(!OfficeConverter::ConvertBytes(Access::make({{"word/document.xml", std::string(1, '\xff')}}),
                                               "docx", transform)
                .success,
                "invalid target UTF-8");
        require(!OfficeConverter::ConvertBytes(Access::make({{"word/document.xml", "old"}}), "docx",
                                               [](std::string_view) { return std::string(1, '\xff'); })
                .success,
                "invalid callback UTF-8");
        const auto good = Access::make({{"word/document.xml", "old"}});
        require(!OfficeConverter::ConvertBytes({}, "docx", transform).success, "empty ZIP");
        require(!OfficeConverter::ConvertBytes(Bytes{'b', 'a', 'd'}, "docx", transform).success,
                "invalid ZIP");
        require(!OfficeConverter::ConvertBytes(good, "pdf", transform).success, "unsupported");
        require(!OfficeConverter::ConvertBytes(good, "docx", {}).success, "empty callback");
        require(!OfficeConverter::ConvertBytes(Access::make({{"metadata.xml", "old"}}), "docx", transform)
                .success,
                "missing targets");
        require(
            !OfficeConverter::ConvertBytes(Access::make({{"book.xhtml", "old"}}), "epub", transform).success,
            "missing mimetype");
        require(!OfficeConverter::ConvertBytes(Access::make({{"book.xhtml", "old"}, {"mimetype", "wrong"}}),
                                               "epub", transform)
                .success,
                "invalid mimetype");
        require(
            !OfficeConverter::ConvertBytes(Access::make({{"../word/document.xml", "old"}}), "docx", transform)
            .success,
            "traversal");
        require(OfficeConverter::ConvertBytes(
                    Access::make({{"word/document.xml", "old"}, {"media/a..b", "old"}}), "docx", transform)
                .success,
                "legitimate dotted name");
        auto corrupt = good;
        constexpr std::array<uint8_t, 3> old{'o', 'l', 'd'};
        const auto where = std::ranges::search(corrupt, old).begin();
        require(where != corrupt.end(), "stored fixture");
        *where = 'x';
        require(!OfficeConverter::ConvertBytes(corrupt, "docx", transform).success, "CRC corruption");
        auto nonTargetCorrupt = Access::make({{"word/document.xml", "ok"}, {"media/data", "old"}});
        const auto nonTarget =
                std::ranges::search(nonTargetCorrupt, old).begin();
        require(nonTarget != nonTargetCorrupt.end(), "non-target fixture");
        *nonTarget = 'x';
        write(input, nonTargetCorrupt);
        write(output, Bytes{'k', 'e', 'e', 'p'});
        auto corruptFile = OfficeConverter::Convert(utf8(input), utf8(output), "docx", transform);
        require(!corruptFile.success && read(output) == Bytes{'k', 'e', 'e', 'p'},
                "non-target CRC validation safety");
        corrupt = good;
        corrupt.resize(corrupt.size() - 12);
        require(!OfficeConverter::ConvertBytes(corrupt, "docx", transform).success, "truncation");

        write(input, good);
        const Bytes sentinel{'k', 'e', 'e', 'p'};
        write(output, sentinel);
        auto fail =
                OfficeConverter::Convert(utf8(input), utf8(output), "docx", [](std::string_view) -> std::string {
                    throw std::runtime_error("injected");
                });
        require(!fail.success && read(output) == sentinel, "callback failure preserves destination");
        require(fileCount(directory) == 2, "ordinary failure cleanup");
        bool rejected = false;
        try {
            Access::invalidValidation(output);
        } catch (...) {
            rejected = true;
        }
        require(rejected && read(output) == sentinel && fileCount(directory) == 2,
                "validation failure safety");

        const auto blocked = directory / "blocked";
        fs::create_directory(blocked);
        write(blocked / "keep", sentinel);
        fail = OfficeConverter::Convert(utf8(input), utf8(blocked), "docx", transform);
        require(!fail.success && read(blocked / "keep") == sentinel && fileCount(directory) == 3,
                "replacement failure safety");
        fs::remove(blocked / "keep");
        fs::remove(blocked);

        require(!OfficeConverter::Convert(utf8(input), utf8(input), "docx", transform).success, "same path");
        require(!OfficeConverter::Convert(utf8(input), utf8(directory / "." / input.filename()), "docx",
                                          transform)
                .success,
                "path alias");
        const auto hardlink = directory / "hardlink.docx";
        fs::create_hard_link(input, hardlink);
        require(!OfficeConverter::Convert(utf8(input), utf8(hardlink), "docx", transform).success,
                "hardlink alias");
        fs::remove(hardlink);
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(output.c_str());
        require(SetFileAttributesW(output.c_str(), attributes | FILE_ATTRIBUTE_READONLY), "set readonly");
        fail = OfficeConverter::Convert(utf8(input), utf8(output), "docx", transform);
        const auto stillReadOnly = GetFileAttributesW(output.c_str()) & FILE_ATTRIBUTE_READONLY;
        SetFileAttributesW(output.c_str(), attributes);
        require(!fail.success && stillReadOnly && read(output) == sentinel, "readonly safety");
        HANDLE lock = CreateFileW(output.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        require(lock != INVALID_HANDLE_VALUE, "lock output");
        fail = OfficeConverter::Convert(utf8(input), utf8(output), "docx", transform);
        CloseHandle(lock);
        require(!fail.success && read(output) == sentinel && fileCount(directory) == 2,
                "sharing failure safety");
#endif
        std::vector<std::future<OfficeConverter::Result> > jobs;
        for (int i = 0; i < 8; ++i) {
            jobs.push_back(std::async(std::launch::async, [&, i] {
                return OfficeConverter::Convert(utf8(input),
                                                utf8(directory / ("parallel-" + std::to_string(i) + ".docx")),
                                                "docx", transform);
            }));
        }
        for (auto &job: jobs) {
            auto result = job.get();
            require(result.success, result.message);
        }
        for (int i = 0; i < 8; ++i) {
            auto parallel = directory / ("parallel-" + std::to_string(i) + ".docx");
            require(Access::unpack(read(parallel))[0].second == "new", "concurrent output");
            fs::remove(parallel);
        }
        require(fileCount(directory) == 2, "concurrent cleanup");
        std::cout << "All OfficeConverter regression checks passed.\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
