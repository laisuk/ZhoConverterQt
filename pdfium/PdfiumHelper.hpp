#pragma once

#include <atomic>
#include <cctype>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <algorithm>
// #include <string_view>

// PDFium public headers.
// Make sure they are in your include path.
#include "fpdfview.h"
#include "fpdf_text.h"

namespace pdfium {
    // ============================================================
    //  PdfiumLibrary (process-wide RAII + global mutex)
    // ============================================================
    class PdfiumLibrary {
    public:
        PdfiumLibrary(const PdfiumLibrary &) = delete;

        PdfiumLibrary &operator=(const PdfiumLibrary &) = delete;

        static PdfiumLibrary &Instance() {
            static PdfiumLibrary instance;
            return instance;
        }

        std::mutex &Mutex() noexcept { return mutex_; }

    private:
        PdfiumLibrary() {
            // Simple init; you can upgrade to FPDF_InitLibraryWithConfig()
            FPDF_InitLibrary();
        }

        ~PdfiumLibrary() {
            FPDF_DestroyLibrary();
        }

        std::mutex mutex_;
    };

    // ============================================================
    //  RAII wrappers: Document & Page
    // ============================================================
    class Document {
    public:
        Document() = default;

        explicit Document(const std::string &path,
                          const std::string &password = {}) {
            Open(path, password);
        }

        ~Document() {
            Reset();
        }

        Document(const Document &) = delete;

        Document &operator=(const Document &) = delete;

        Document(Document &&other) noexcept
            : handle_(other.handle_) {
            other.handle_ = nullptr;
        }

        Document &operator=(Document &&other) noexcept {
            if (this != &other) {
                Reset();
                handle_ = other.handle_;
                other.handle_ = nullptr;
            }
            return *this;
        }

        void Open(const std::string &path,
                  const std::string &password = {}) {
            Reset();

            auto &lib = PdfiumLibrary::Instance();
            std::lock_guard lock(lib.Mutex());

            handle_ = FPDF_LoadDocument(
                path.c_str(),
                password.empty() ? nullptr : password.c_str());

            if (!handle_) {
                const unsigned long err = FPDF_GetLastError();
                throw std::runtime_error("FPDF_LoadDocument failed, error = " +
                                         std::to_string(err));
            }
        }

        void Reset() noexcept {
            if (handle_) {
                auto &lib = PdfiumLibrary::Instance();
                std::lock_guard<std::mutex> lock(lib.Mutex());
                FPDF_CloseDocument(handle_);
                handle_ = nullptr;
            }
        }

        [[nodiscard]] bool IsValid() const noexcept { return handle_ != nullptr; }
        [[nodiscard]] FPDF_DOCUMENT Get() const noexcept { return handle_; }

        [[nodiscard]] int GetPageCount() const {
            if (!handle_)
                return 0;

            auto &lib = PdfiumLibrary::Instance();
            std::lock_guard<std::mutex> lock(lib.Mutex());
            return FPDF_GetPageCount(handle_);
        }

    private:
        FPDF_DOCUMENT handle_ = nullptr;
    };

    class Page {
    public:
        Page() = default;

        Page(FPDF_DOCUMENT doc, const int index) {
            Open(doc, index);
        }

        ~Page() {
            Reset();
        }

        Page(const Page &) = delete;

        Page &operator=(const Page &) = delete;

        Page(Page &&other) noexcept
            : handle_(other.handle_) {
            other.handle_ = nullptr;
        }

        Page &operator=(Page &&other) noexcept {
            if (this != &other) {
                Reset();
                handle_ = other.handle_;
                other.handle_ = nullptr;
            }
            return *this;
        }

        void Open(FPDF_DOCUMENT doc, const int index) {
            Reset();
            if (!doc)
                throw std::runtime_error("Page::Open: null document handle");

            auto &lib = PdfiumLibrary::Instance();
            std::lock_guard lock(lib.Mutex());

            handle_ = FPDF_LoadPage(doc, index);
            if (!handle_)
                throw std::runtime_error("FPDF_LoadPage failed at index " +
                                         std::to_string(index));
        }

        void Reset() noexcept {
            if (handle_) {
                auto &lib = PdfiumLibrary::Instance();
                std::lock_guard<std::mutex> lock(lib.Mutex());
                FPDF_ClosePage(handle_);
                handle_ = nullptr;
            }
        }

        [[nodiscard]] bool IsValid() const noexcept { return handle_ != nullptr; }
        [[nodiscard]] FPDF_PAGE Get() const noexcept { return handle_; }

        [[nodiscard]] double Width() const {
            if (!handle_)
                return 0.0;

            auto &lib = PdfiumLibrary::Instance();
            std::lock_guard<std::mutex> lock(lib.Mutex());
            return FPDF_GetPageWidth(handle_);
        }

        [[nodiscard]] double Height() const {
            if (!handle_)
                return 0.0;

            auto &lib = PdfiumLibrary::Instance();
            std::lock_guard<std::mutex> lock(lib.Mutex());
            return FPDF_GetPageHeight(handle_);
        }

    private:
        FPDF_PAGE handle_ = nullptr;
    };

    // ============================================================
    //  Internal helpers: UTF-16LE -> UTF-8 + progress bar
    // ============================================================

    namespace detail {
        inline void append_utf8_codepoint(std::string &out, const std::uint32_t cp) {
            if (cp <= 0x7F) {
                out.push_back(static_cast<char>(cp));
            } else if (cp <= 0x7FF) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp <= 0xFFFF) {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }

        inline void utf16le_to_utf8_into(const std::u16string &src, std::string &out) {
            out.clear();
            out.reserve(src.size() * 3); // rough estimate

            for (std::size_t i = 0; i < src.size(); ++i) {
                const std::uint32_t ch = src[i];

                if (ch >= 0xD800 && ch <= 0xDBFF) {
                    // High surrogate
                    if (i + 1 < src.size()) {
                        const std::uint32_t low = src[i + 1];
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            const std::uint32_t cp =
                                    0x10000 + (((ch - 0xD800) << 10) | (low - 0xDC00));
                            append_utf8_codepoint(out, cp);
                            ++i; // consumed low surrogate
                            continue;
                        }
                    }
                    // Malformed surrogate
                    append_utf8_codepoint(out, 0xFFFD);
                } else if (ch >= 0xDC00 && ch <= 0xDFFF) {
                    // Lone low surrogate
                    append_utf8_codepoint(out, 0xFFFD);
                } else {
                    append_utf8_codepoint(out, ch);
                }
            }
        }

        // In-place CRLF/CR -> LF normalization without allocating a second string.
        inline void NormalizeNewlinesInPlace(std::string &s) {
            std::size_t write = 0;
            const std::size_t n = s.size();

            for (std::size_t read = 0; read < n; ++read) {
                const char c = s[read];
                if (c == '\r') {
                    if (read + 1 < n && s[read + 1] == '\n')
                        ++read;
                    s[write++] = '\n';
                } else {
                    s[write++] = c;
                }
            }

            s.resize(write);
        }

        // Returns [start, end) trim range without allocating.
        inline std::pair<std::size_t, std::size_t> TrimRange(const std::string &s) {
            std::size_t start = 0;
            std::size_t end = s.size();

            while (start < end &&
                   std::isspace(static_cast<unsigned char>(s[start]))) {
                ++start;
            }

            while (end > start &&
                   std::isspace(static_cast<unsigned char>(s[end - 1]))) {
                --end;
            }

            return {start, end};
        }

        // Emoji progress bar:
        // 🟩 = U+1F7E9 = F0 9F 9F A9
        // ⬜ = U+2B1C  = E2 AC 9C
        inline std::string BuildProgressBar(int percent, const int width = 10) {
            if (percent < 0) percent = 0;
            if (percent > 100) percent = 100;

            const int filled = (percent * width) / 100;

            static constexpr auto GREEN = "\xF0\x9F\x9F\xA9";
            static constexpr auto WHITE = "\xE2\xAC\x9C";

            std::string bar;
            bar.reserve(static_cast<std::size_t>(width) * 4);

            for (int i = 0; i < filled; ++i)
                bar += GREEN;
            for (int i = filled; i < width; ++i)
                bar += WHITE;

            return bar;
        }

        // Extract all text from a page into reusable buffers.
        inline void ExtractPageText(FPDF_PAGE page,
                                    std::u16string &utf16Buffer,
                                    std::string &utf8Out) {
            utf16Buffer.clear();
            utf8Out.clear();

            if (!page)
                return;

            auto &lib = PdfiumLibrary::Instance();
            std::lock_guard lock(lib.Mutex());

            // IMPORTANT:
            // Pdfium handles (FPDF_PAGE, FPDF_TEXTPAGE, FPDF_DOCUMENT, etc.)
            // must NEVER be declared as `const`.
            //
            // They are opaque pointers that Pdfium may mutate internally.
            // Adding `const` will break compatibility with the Pdfium C API
            // and may cause undefined behavior when Pdfium expects a mutable handle.
            //
            // Always keep them as raw, non-const handles.
            FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
            if (!textPage)
                return;

            const int nChars = FPDFText_CountChars(textPage);
            if (nChars <= 0) {
                FPDFText_ClosePage(textPage);
                return;
            }

            utf16Buffer.resize(static_cast<std::size_t>(nChars) + 1);

            int written = FPDFText_GetText(
                textPage,
                0,
                nChars,
                reinterpret_cast<unsigned short *>(utf16Buffer.data()));

            FPDFText_ClosePage(textPage);

            if (written <= 0)
                return;

            // Pdfium returns a count including the terminating NUL.
            if (written > nChars + 1)
                written = nChars + 1;

            const int actualChars = (written > 0) ? (written - 1) : 0;
            if (actualChars <= 0) {
                utf16Buffer.clear();
                return;
            }

            utf16Buffer.resize(static_cast<std::size_t>(actualChars));
            utf16le_to_utf8_into(utf16Buffer, utf8Out);
        }
    } // namespace detail

    // ============================================================
    //  High-level text extraction API
    // ============================================================

    // Progress callback:
    //   pageIndex  : 0-based page index
    //   pageCount  : total pages
    //   percent    : completion percentage (0..100)
    //   bar        : emoji progress bar (🟩⬜⬜...)
    using ProgressCallback =
    std::function<void(int pageIndex,
                       int pageCount,
                       int percent,
                       const std::string &bar)>;

    // Synchronous extraction.
    // - path            : path to PDF file
    // - addPageHeader   : if true, prepend "=== [Page x/N] ===" before each page
    // - progress        : optional progress callback
    // - cancelFlag      : optional atomic<bool> for cancellation
    //
    // Returns full UTF-8 text content.
    inline std::string ExtractText(const std::string &path,
                                   const bool addPageHeader = true,
                                   const ProgressCallback &progress = nullptr,
                                   const std::atomic<bool> *cancelFlag = nullptr) {
        // Ensure library is initialized
        (void) PdfiumLibrary::Instance();

        const Document doc(path);
        const int pageCount = doc.GetPageCount();

        if (pageCount <= 0)
            return {};

        std::string result;
        result.reserve(static_cast<std::size_t>(pageCount) * 1024);

        // Reusable per-page buffers
        std::u16string utf16Buffer;
        std::string pageText;

        for (int i = 0; i < pageCount; ++i) {
            if (cancelFlag &&
                cancelFlag->load(std::memory_order_relaxed)) {
                break;
            }

            Page page(doc.Get(), i);

            if (addPageHeader) {
                result += "=== [Page ";
                result += std::to_string(i + 1);
                result += "/";
                result += std::to_string(pageCount);
                result += "] ===\n\n";
            }

            // Extract text for this page into reusable buffers
            detail::ExtractPageText(page.Get(), utf16Buffer, pageText);
            detail::NormalizeNewlinesInPlace(pageText);

            const auto [start, end] = detail::TrimRange(pageText);
            if (end > start) {
                result.append(pageText, start, end - start);
            }

            result += "\n\n";

            if (progress) {
                const int percent =
                        static_cast<int>(
                            (static_cast<double>(i + 1) / static_cast<double>(pageCount)) * 100.0);

                std::string bar = detail::BuildProgressBar(percent);
                progress(i, pageCount, percent, bar);
            }
        }

        return result;
    }

    // Asynchronous extraction.
    //
    // - path          : path to PDF file
    // - addPageHeader : same meaning as synchronous version
    // - progress      : optional progress callback (called from worker thread)
    // - cancelFlag    : shared cancellation flag (if nullptr, one is created)
    //
    // Returns std::future<std::string> with the final text.
    inline std::future<std::string>
    ExtractTextAsync(std::string path,
                     bool addPageHeader = true,
                     const ProgressCallback &progress = nullptr,
                     std::shared_ptr<std::atomic<bool> > cancelFlag = nullptr) {
        if (!cancelFlag)
            cancelFlag = std::make_shared<std::atomic<bool> >(false);

        return std::async(std::launch::async,
                          [path = std::move(path),
                              addPageHeader,
                              progress,
                              cancelFlag]() -> std::string {
                              try {
                                  return ExtractText(path,
                                                     addPageHeader,
                                                     progress,
                                                     cancelFlag.get());
                              } catch (...) {
                                  // For GUI integration, you may want to rethrow and handle outside.
                                  // throw;
                                  return "";
                              }
                          });
    }
} // namespace pdfium
