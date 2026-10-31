// Build the same driver against either header; no timing hooks in production.
#ifdef OFFICE_CONVERTER_HEADER
#include OFFICE_CONVERTER_HEADER
#else
#include "../../src/OfficeConverter.hpp"
#endif
#include <chrono>
#include <iostream>
#ifdef _WIN32
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

struct OfficeConverterTestAccess {
    using Clock = std::chrono::steady_clock;
    static double elapsed(const Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }
    static void fixture(const std::filesystem::path &path, size_t textBytes, size_t binaryBytes,
                        size_t entries, bool deflate) {
        auto archive = OfficeConverter::openFile(path, ZIP_CREATE | ZIP_TRUNCATE);
        std::string text(textBytes, 'x');
        text.replace(0, 3, "old");
        OfficeConverter::addBuffer(archive.get(), "word/document.xml", text);
        // Deterministic, poorly compressible resource; generation is not timed.
        std::string binary(binaryBytes, '\0');
        uint32_t state = 123456789;
        for (auto &c: binary) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            c = static_cast<char>(state);
        }
        if (binaryBytes) {
            OfficeConverter::addBuffer(archive.get(), "media/resource.bin", binary);
            if (zip_set_file_compression(archive.get(), 1, deflate ? ZIP_CM_DEFLATE : ZIP_CM_STORE, 0) < 0)
                throw std::runtime_error("fixture compression");
        }
        for (size_t i = 0; i < entries; ++i)
            OfficeConverter::addBuffer(archive.get(), "media/item-" + std::to_string(i), "resource");
        OfficeConverter::finish(archive);
    }
    // Conversion includes preflight and spool writes; finalization is zip_close;
    // validation includes reopening with ZIP_CHECKCONS and strict entry draining.
    static std::array<double, 3> stages(const std::filesystem::path &input,
                                       const std::filesystem::path &output,
                                       const TextConverter &converter, bool keepFont) {
        const auto in = OfficeConverter::openFile(input, ZIP_RDONLY | ZIP_CHECKCONS);
        OfficeConverter::TempOutput temp(output);
        const OfficeConverter::TempOutput spool(output);
        auto out = OfficeConverter::openFile(temp.path, ZIP_CREATE | ZIP_TRUNCATE);
        auto start = Clock::now();
        OfficeConverter::convertArchive(in.get(), out.get(), "docx", converter, keepFont, &spool.path);
        const auto convert = elapsed(start);
        start = Clock::now();
        OfficeConverter::finish(out);
        const auto finish = elapsed(start);
        start = Clock::now();
        {
            const auto valid = OfficeConverter::openFile(temp.path, ZIP_RDONLY | ZIP_CHECKCONS);
            OfficeConverter::validate(valid.get());
        }
        return {convert, finish, elapsed(start)};
    }
};

static std::string utf8(const std::filesystem::path &path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

int main(int argc, char **argv) {
    try {
        // Run one workload per process so peak memory is attributable to it.
        const std::string workload = argc > 1 ? argv[1] : "small";
        const int runs = argc > 2 ? std::stoi(argv[2]) : 7;
        if (runs < 1)
            throw std::runtime_error("runs must be positive");
        size_t text = 8192, binary = 0, entries = 0;
        bool keepFont = true, deflate = false;
        if (workload == "medium") { text = 128 * 1024; binary = 4 * 1024 * 1024; }
        else if (workload == "large") { binary = 32 * 1024 * 1024; }
        else if (workload == "deflated") { binary = 32 * 1024 * 1024; deflate = true; }
        else if (workload == "text") { text = 2 * 1024 * 1024; keepFont = false; }
        else if (workload == "catalog") { entries = 2000; }
        else if (workload != "small")
            throw std::runtime_error("unknown workload");
        namespace fs = std::filesystem;
        const auto directory = fs::current_path() / ("office-benchmark-" + std::to_string(std::random_device{}()));
        fs::create_directory(directory);
        struct Cleanup {
            fs::path path;
            ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
        } cleanup{directory};
        const auto input = directory / "input.docx", output = directory / "output.docx";
        using Access = OfficeConverterTestAccess;
        Access::fixture(input, text, binary, entries, deflate);
        std::ifstream stream(input, std::ios::binary);
        const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        const TextConverter converter = [](std::string_view value) {
            std::string result(value);
            result.replace(0, 3, "new");
            return result;
        };
        std::array<std::vector<double>, 5> times;
        for (int run = -1; run < runs; ++run) {
            // Each iteration gets its own destination; replacement contention
            // from filesystem scanners is unrelated to pipeline performance.
            const auto destination = directory / ("output-" + std::to_string(run) + ".docx");
            const auto stages = Access::stages(input, output, converter, keepFont);
            auto start = Access::Clock::now();
            const auto file = OfficeConverter::Convert(utf8(input), utf8(destination), "docx", converter, keepFont);
            const auto fileMs = Access::elapsed(start);
            if (!file.success) throw std::runtime_error(file.message);
            start = Access::Clock::now();
            const auto memory = OfficeConverter::ConvertBytes(bytes, "docx", converter, keepFont);
            const auto memoryMs = Access::elapsed(start);
            if (!memory.success) throw std::runtime_error(memory.message);
            if (run < 0) continue; // warm-up
            for (size_t i = 0; i < stages.size(); ++i) times[i].push_back(stages[i]);
            times[3].push_back(fileMs);
            times[4].push_back(memoryMs);
        }
        std::cout << workload;
        for (auto &values: times) {
            std::sort(values.begin(), values.end());
            const size_t middle = values.size() / 2;
            const auto median = values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
            std::cout << ',' << median;
        }
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS counters{};
        if (!K32GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
            throw std::runtime_error("peak memory unavailable");
        std::cout << ',' << counters.PeakWorkingSetSize / 1024;
#else
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage) != 0) throw std::runtime_error("peak memory unavailable");
#ifdef __APPLE__
        std::cout << ',' << usage.ru_maxrss / 1024;
#else
        std::cout << ',' << usage.ru_maxrss;
#endif
#endif
        std::cout << '\n';
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
