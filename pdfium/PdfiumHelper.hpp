#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
// #include <vector>

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
            std::lock_guard<std::mutex> lock(lib.Mutex());

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
            std::lock_guard<std::mutex> lock(lib.Mutex());

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
            if (!handle_) return 0.0;
            auto &lib = PdfiumLibrary::Instance();
            std::lock_guard<std::mutex> lock(lib.Mutex());
            return FPDF_GetPageWidth(handle_);
        }

        [[nodiscard]] double Height() const {
            if (!handle_) return 0.0;
            auto &lib = PdfiumLibrary::Instance();
            std::lock_guard<std::mutex> lock(lib.Mutex());
            return FPDF_GetPageHeight(handle_);
        }

    private:
        FPDF_PAGE handle_ = nullptr;
    };

    // ============================================================
    //  Internal helpers: UTF-16LE → UTF-8 + progress bar
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

        inline std::string utf16le_to_utf8(const std::u16string &src) {
            std::string out;
            out.reserve(src.size() * 3); // rough estimate

            for (std::size_t i = 0; i < src.size(); ++i) {
                if (const std::uint32_t ch = src[i]; ch >= 0xD800 && ch <= 0xDBFF) {
                    // High surrogate
                    if (i + 1 < src.size()) {
                        if (const std::uint32_t low = src[i + 1]; low >= 0xDC00 && low <= 0xDFFF) {
                            const std::uint32_t cp =
                                    0x10000 +
                                    (((ch - 0xD800) << 10) | (low - 0xDC00));
                            append_utf8_codepoint(out, cp);
                            ++i; // consumed low surrogate
                            continue;
                        }
                    }
                    // Malformed surrogate: fall back to replacement char
                    append_utf8_codepoint(out, 0xFFFD);
                } else if (ch >= 0xDC00 && ch <= 0xDFFF) {
                    // Lone low surrogate
                    append_utf8_codepoint(out, 0xFFFD);
                } else {
                    append_utf8_codepoint(out, ch);
                }
            }

            return out;
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
            bar.reserve(width * 4);

            for (int i = 0; i < filled; ++i)
                bar += GREEN;
            for (int i = filled; i < width; ++i)
                bar += WHITE;

            return bar;
        }

        // Extract all text from a page (UTF-8)
        inline std::string ExtractPageText(FPDF_PAGE page) {
            if (!page)
                return {};

            auto &lib = PdfiumLibrary::Instance();
            std::lock_guard<std::mutex> lock(lib.Mutex());

            FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page);
            if (!textPage)
                return {};

            const int nChars = FPDFText_CountChars(textPage);
            if (nChars <= 0) {
                FPDFText_ClosePage(textPage);
                return {};
            }

            std::u16string buffer(static_cast<std::size_t>(nChars) + 1, u'\0');

            int written = FPDFText_GetText(
                textPage,
                0,
                nChars,
                reinterpret_cast<unsigned short *>(buffer.data()));

            FPDFText_ClosePage(textPage);

            if (written <= 0)
                return {};

            // |written| includes terminating NUL
            if (written > nChars)
                written = nChars;

            buffer.resize(static_cast<std::size_t>(written));
            return utf16le_to_utf8(buffer);
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
        result.reserve(16 * 1024); // basic reserve; will grow as needed

        for (int i = 0; i < pageCount; ++i) {
            if (cancelFlag &&
                cancelFlag->load(std::memory_order_relaxed)) {
                break;
            }

            Page page(doc.Get(), i);

            if (addPageHeader) {
                // Example: === [Page 1/220] ===
                result += "=== [Page ";
                result += std::to_string(i + 1);
                result += "/";
                result += std::to_string(pageCount);
                result += "] ===\n\n";
            }

            // Extract text for this page
            const std::string pageText = detail::ExtractPageText(page.Get());
            result += pageText;
            result += "\n\n";

            // Progress callback
            if (progress) {
                const int percent =
                        static_cast<int>((static_cast<double>(i + 1) /
                                          static_cast<double>(pageCount)) * 100.0);
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
                                  throw;
                              }
                          });
    }
} // namespace pdfium


// -----------------------------------------------------------------------------
// CJK paragraph reflow (ported from pdf_helper.py::reflow_cjk_paragraphs_core)
// -----------------------------------------------------------------------------
//
// Public API:
//
//   std::string ReflowCjkParagraphs(const std::string& utf8Text,
//                                   bool addPdfPageHeader,
//                                   bool compact);
//
// This function assumes `utf8Text` is UTF-8. It decodes to char32_t for
// punctuation and dialog logic, then re-encodes to UTF-8.
//

#include <vector>
#include <unordered_map>
#include <algorithm>

namespace pdfium {
    namespace detail {
        // ---------- UTF-8 <-> UTF-32 helpers (minimal, enough for CJK text) ----------

        inline std::u32string Utf8ToU32(const std::string &s) {
            std::u32string out;
            out.reserve(s.size());

            auto p = reinterpret_cast<const unsigned char *>(s.data());
            const unsigned char *end = p + s.size();

            while (p < end) {
                uint32_t ch = 0;

                if (const unsigned char c = *p++; c < 0x80) {
                    ch = c;
                } else if ((c >> 5) == 0x6 && p < end) {
                    // 110xxxxx
                    ch = ((c & 0x1F) << 6);
                    ch |= (*p++ & 0x3F);
                } else if ((c >> 4) == 0xE && p + 1 < end) {
                    // 1110xxxx
                    ch = ((c & 0x0F) << 12);
                    ch |= ((p[0] & 0x3F) << 6);
                    ch |= ((p[1] & 0x3F));
                    p += 2;
                } else if ((c >> 3) == 0x1E && p + 2 < end) {
                    // 11110xxx
                    ch = ((c & 0x07) << 18);
                    ch |= ((p[0] & 0x3F) << 12);
                    ch |= ((p[1] & 0x3F) << 6);
                    ch |= ((p[2] & 0x3F));
                    p += 3;
                } else {
                    // Invalid sequence: skip or insert replacement char
                    ch = 0xFFFD; // �
                }

                out.push_back(static_cast<char32_t>(ch));
            }

            return out;
        }

        inline std::string U32ToUtf8(const std::u32string &s) {
            std::string out;
            out.reserve(s.size() * 3);

            for (const char32_t ch: s) {
                if (const auto c = static_cast<uint32_t>(ch); c < 0x80) {
                    out.push_back(static_cast<char>(c));
                } else if (c < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (c >> 6)));
                    out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
                } else if (c < 0x10000) {
                    out.push_back(static_cast<char>(0xE0 | (c >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
                } else {
                    out.push_back(static_cast<char>(0xF0 | (c >> 18)));
                    out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
                }
            }

            return out;
        }

        // ------------------------- Tables / constants -------------------------

        // CJK punctuation / title rules (from pdf_helper.py CJK_PUNCT_END)
        static const std::u32string CJK_PUNCT_END = U"。！？；：…”」’』）】》〗〕〉］｝.!?)";

        // Dialog brackets (from DIALOG_OPEN_TO_CLOSE)
        static const std::u32string DIALOG_OPENERS = U"“‘「『";
        static const std::u32string DIALOG_CLOSERS = U"”’」』";

        // Brackets for heading check
        static const std::u32string OPEN_BRACKETS = U"（([【《";
        static const std::u32string CLOSE_BRACKETS = U"）)]】》";

        // Title heading patterns (see TITLE_HEADING_REGEX)
        static const std::u32string TITLE_WORDS[] = {
            U"前言", U"序章", U"终章", U"尾声", U"后记",
            U"番外", U"尾聲", U"後記"
        };
        static const std::u32string CHAPTER_MARKERS = U"章节部卷節回";

        // Closing bracket chars for chapter-ending rule
        static const std::u32string CHAPTER_END_BRACKETS = U"】》〗〕〉」』）";

        // Utility: contains
        inline bool Contains(const std::u32string &s, const char32_t ch) {
            return std::find(s.begin(), s.end(), ch) != s.end();
        }

        // Utility: startswith "=== " && endswith "==="
        inline bool IsPageMarker(const std::u32string &s) {
            if (s.size() < 7) return false; // "=== x ==="
            return s.rfind(U"=== ", 0) == 0 && s.size() >= 3 &&
                   s[s.size() - 1] == U'=' &&
                   s[s.size() - 2] == U'=' &&
                   s[s.size() - 3] == U'=';
        }

        // Trim helpers (simple)
        inline std::u32string RStrip(const std::u32string &s) {
            std::size_t end = s.size();
            while (end > 0 && (s[end - 1] == U' ' || s[end - 1] == U'\t' || s[end - 1] == U'\r')) {
                --end;
            }
            return s.substr(0, end);
        }

        inline std::u32string LStrip(const std::u32string &s) {
            std::size_t pos = 0;
            while (pos < s.size() && (s[pos] == U' ' || s[pos] == U'\t' || s[pos] == U'\u3000')) {
                ++pos;
            }
            return s.substr(pos);
        }

        inline std::u32string Strip(const std::u32string &s) {
            return RStrip(LStrip(s));
        }

        // Length in codepoints
        inline std::size_t Len(const std::u32string &s) { return s.size(); }

        // Contains any char from set
        inline bool AnyOf(const std::u32string &s, const std::u32string &set) {
            for (const char32_t ch: s) {
                if (Contains(set, ch)) return true;
            }
            return false;
        }

        // Contains any CJK (very simple heuristic: >0x7F)
        inline bool ContainsCjk(const std::u32string &s) {
            for (const char32_t ch: s) {
                if (ch > 0x7F) return true;
            }
            return false;
        }

        // All ASCII?
        inline bool IsAllAscii(const std::u32string &s) {
            for (const char32_t ch: s) {
                if (ch > 0x7F) return false;
            }
            return true;
        }

        // Any A-Z / a-z
        inline bool HasLatinAlpha(const std::u32string &s) {
            for (const char32_t ch: s) {
                if ((ch >= U'a' && ch <= U'z') || (ch >= U'A' && ch <= U'Z'))
                    return true;
            }
            return false;
        }

        // Collapse repeated segments (token-level), from collapse_repeated_segments()
        inline std::u32string CollapseRepeatedToken(const std::u32string &token) {
            const std::size_t length = token.size();
            if (length < 4 || length > 200) return token;

            for (std::size_t unit_len = 2; unit_len <= 20; ++unit_len) {
                if (unit_len > length / 2) break;
                if (length % unit_len != 0) continue;

                const std::u32string unit = token.substr(0, unit_len);
                const std::size_t repeat = length / unit_len;

                std::u32string repeated;
                repeated.reserve(length);
                for (std::size_t i = 0; i < repeat; ++i)
                    repeated += unit;

                if (repeated == token)
                    return unit;
            }
            return token;
        }

        inline std::u32string CollapseRepeatedSegments(const std::u32string &line) {
            // split on spaces/tabs
            std::vector<std::u32string> parts;
            std::u32string current;
            for (const char32_t ch: line) {
                if (ch == U' ' || ch == U'\t') {
                    if (!current.empty()) {
                        parts.push_back(current);
                        current.clear();
                    }
                } else {
                    current.push_back(ch);
                }
            }
            if (!current.empty())
                parts.push_back(current);

            if (parts.empty())
                return line;

            std::u32string out;
            bool first = true;
            for (auto &tok: parts) {
                const auto collapsed = CollapseRepeatedToken(tok);
                if (!first) out.push_back(U' ');
                out += collapsed;
                first = false;
            }
            return out;
        }

        // ------------------------- DialogState -------------------------

        struct DialogState {
            // counts for each opener
            std::unordered_map<char32_t, int> counts;

            DialogState() {
                for (char32_t ch: DIALOG_OPENERS) {
                    counts[ch] = 0;
                }
            }

            void reset() {
                for (auto &[key, val]: counts) {
                    val = 0;
                }
            }

            void update(const std::u32string &s) {
                for (char32_t ch: s) {
                    if (auto it = std::find(DIALOG_OPENERS.begin(), DIALOG_OPENERS.end(), ch);
                        it != DIALOG_OPENERS.end()) {
                        counts[ch] += 1;
                    } else {
                        // if it's a closer, map back to opener
                        if (const std::size_t idx = DIALOG_CLOSERS.find(ch); idx != std::u32string::npos) {
                            char32_t open_ch = DIALOG_OPENERS[idx];
                            if (auto it2 = counts.find(open_ch); it2 != counts.end() && it2->second > 0)
                                it2->second -= 1;
                        }
                    }
                }
            }

            bool is_unclosed() const {
                for (const auto &[key, val]: counts) {
                    if (val > 0) return true;
                }
                return false;
            }
        };

        // ------------------------- Title & heading heuristics -------------------------

        inline bool IsTitleHeading(const std::u32string &s_left) {
            // Equivalent to TITLE_HEADING_REGEX: length <= 60 and
            //   starts with fixed title word OR "第 ... [章节部卷節回]"
            const std::size_t len = s_left.size();
            if (len == 0 || len > 60) return false;

            // fixed words
            for (const auto &w: TITLE_WORDS) {
                if (s_left.rfind(w, 0) == 0) {
                    return true;
                }
            }

            // "第 ... [章节部卷節回]"
            if (s_left[0] == U'第') {
                // find last non-space char
                std::size_t pos = len;
                while (pos > 1 && (s_left[pos - 1] == U' ' || s_left[pos - 1] == U'\u3000')) {
                    --pos;
                }
                if (pos > 1) {
                    if (const char32_t last = s_left[pos - 1]; Contains(CHAPTER_MARKERS, last)) {
                        // limit middle length to <= 10 (like regex)
                        if (pos - 1 <= 1 + 10)
                            return true;
                    }
                }
            }

            return false;
        }

        inline bool IsDialogStart(const std::u32string &line) {
            const auto s = LStrip(line);
            if (s.empty()) return false;
            return DIALOG_OPENERS.find(s[0]) != std::u32string::npos;
        }

        inline bool HasOpenBracketNoClose(const std::u32string &s) {
            if (const bool hasOpen = AnyOf(s, OPEN_BRACKETS); !hasOpen) return false;
            if (AnyOf(s, CLOSE_BRACKETS)) return false;
            return true;
        }

        inline bool IsHeadingLike(const std::u32string &raw) {
            // Port of is_heading_like() docstring logic
            const std::u32string s = Strip(raw);
            if (s.empty()) return false;

            // Keep page markers intact
            if (IsPageMarker(s)) return false;

            // If contains CJK end punctuation anywhere, not heading/emphasis
            if (AnyOf(s, CJK_PUNCT_END)) return false;

            // If line has an opening bracket but no closing bracket:
            if (HasOpenBracketNoClose(s)) return false;

            const std::size_t len = s.size();

            // Rule A: short CJK/mixed lines
            if (len <= 15 && ContainsCjk(s)) {
                if (const char32_t last = s[len - 1]; !(last == U'，' || last == U',')) {
                    return true;
                }
            }

            // Rule B: short pure ASCII emphasis
            if (len <= 15 && IsAllAscii(s) && HasLatinAlpha(s)) {
                return true;
            }

            return false;
        }

        // Indentation: "^\s{2,}" - we approximate: at least 2 leading spaces/full-width spaces
        inline bool IsIndented(const std::u32string &raw_line) {
            int count = 0;
            for (const char32_t ch: raw_line) {
                if (ch == U' ' || ch == U'\t' || ch == U'\u3000') {
                    ++count;
                    if (count >= 2) return true;
                } else {
                    break;
                }
            }
            return false;
        }

        // Chapter-like ending: short line ending with 章 / 节 / 部 / 卷 / 節, with trailing brackets
        inline bool IsChapterEnding(const std::u32string &s) {
            if (s.size() > 15) return false;
            // strip trailing closing brackets
            std::size_t end = s.size();
            while (end > 0 && Contains(CHAPTER_END_BRACKETS, s[end - 1])) {
                --end;
            }
            if (end == 0) return false;
            if (const char32_t last = s[end - 1]; !Contains(CHAPTER_MARKERS, last)) return false;
            return true;
        }

        // ------------------------- Core reflow implementation -------------------------

        inline std::string ReflowCjkParagraphs(const std::string &utf8Text,
                                               bool addPdfPageHeader,
                                               bool compact) {
            // Empty/whitespace text → return as-is
            bool allSpace = true;
            for (char ch: utf8Text) {
                if (!(ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')) {
                    allSpace = false;
                    break;
                }
            }
            if (allSpace) {
                return utf8Text;
            }

            // Normalize line endings
            std::string norm = utf8Text;
            // replace CRLF and CR with LF
            {
                std::string tmp;
                tmp.reserve(norm.size());
                for (std::size_t i = 0; i < norm.size(); ++i) {
                    if (char c = norm[i]; c == '\r') {
                        if (i + 1 < norm.size() && norm[i + 1] == '\n') {
                            // skip this CR, LF will be handled next iteration
                            continue;
                        }
                        tmp.push_back('\n');
                    } else {
                        tmp.push_back(c);
                    }
                }
                norm.swap(tmp);
            }

            // Split into lines (UTF-8) and convert each to UTF-32
            std::vector<std::u32string> lines32; {
                std::string current;
                for (char ch: norm) {
                    if (ch == '\n') {
                        lines32.push_back(Utf8ToU32(current));
                        current.clear();
                    } else {
                        current.push_back(ch);
                    }
                }
                // last line
                lines32.push_back(Utf8ToU32(current));
            }

            std::vector<std::u32string> segments;
            std::u32string buffer;
            DialogState dialog_state;

            auto flush_buffer = [&]() {
                if (!buffer.empty()) {
                    segments.push_back(buffer);
                    buffer.clear();
                    dialog_state.reset();
                }
            };

            for (const auto &raw_line32: lines32) {
                std::u32string stripped = RStrip(raw_line32);
                std::u32string stripped_left = LStrip(stripped);

                bool is_title_heading = IsTitleHeading(stripped_left);

                // style-layer repeated titles collapse
                if (is_title_heading) {
                    stripped = CollapseRepeatedSegments(stripped);
                }

                // 1) Empty line
                if (stripped.empty()) {
                    if (!addPdfPageHeader && !buffer.empty()) {
                        if (char32_t last_char = buffer.back(); !Contains(CJK_PUNCT_END, last_char)) {
                            // treat as layout gap, skip
                            continue;
                        }
                    }

                    // End of paragraph -> flush buffer, no empty segment
                    flush_buffer();
                    continue;
                }

                // 2) Page markers === [Page x/y] ===
                if (IsPageMarker(stripped)) {
                    flush_buffer();
                    segments.push_back(stripped);
                    continue;
                }

                // 3) Title heading
                if (is_title_heading) {
                    flush_buffer();
                    segments.push_back(stripped);
                    continue;
                }

                bool current_is_dialog_start = IsDialogStart(stripped);

                // 4) First line of new paragraph
                if (buffer.empty()) {
                    buffer = stripped;
                    dialog_state.reset();
                    dialog_state.update(stripped);
                    continue;
                }

                std::u32string &buffer_text = buffer;

                // Dialog rule: if this line starts with dialog opener → new paragraph
                if (current_is_dialog_start) {
                    flush_buffer();
                    buffer = stripped;
                    dialog_state.update(stripped);
                    continue;
                }

                // Colon + dialog continuation: "她写了一行字：" + 「如果连自己都不相信……」
                if (!buffer_text.empty() &&
                    (buffer_text.back() == U'：' || buffer_text.back() == U':') &&
                    !stripped.empty() &&
                    Contains(DIALOG_OPENERS, stripped[0])) {
                    buffer += stripped;
                    dialog_state.update(stripped);
                    continue;
                }

                // 5) Ends with CJK punctuation → new paragraph if not inside unclosed dialog
                if (!buffer_text.empty() &&
                    Contains(CJK_PUNCT_END, buffer_text.back()) &&
                    !dialog_state.is_unclosed()) {
                    flush_buffer();
                    buffer = stripped;
                    dialog_state.update(stripped);
                    continue;
                }

                // 6) Previous buffer looks like heading-like short title
                if (IsHeadingLike(buffer_text)) {
                    flush_buffer();
                    buffer = stripped;
                    dialog_state.update(stripped);
                    continue;
                }

                // 7) Indentation -> new paragraph
                if (IsIndented(raw_line32)) {
                    flush_buffer();
                    buffer = stripped;
                    dialog_state.update(stripped);
                    continue;
                }

                // 8) Chapter-like endings
                if (IsChapterEnding(buffer_text)) {
                    flush_buffer();
                    buffer = stripped;
                    dialog_state.update(stripped);
                    continue;
                }

                // 9) Default: merge (soft line break)
                buffer += stripped;
                dialog_state.update(stripped);
            }

            // Flush last buffer
            if (!buffer.empty()) {
                segments.push_back(buffer);
            }

            // Join segments with one or two blank lines
            std::u32string result32;
            for (std::size_t i = 0; i < segments.size(); ++i) {
                if (i > 0) {
                    if (compact) {
                        result32.push_back(U'\n');
                    } else {
                        result32.push_back(U'\n');
                        result32.push_back(U'\n');
                    }
                }
                result32 += segments[i];
            }

            return U32ToUtf8(result32);
        }
    } // namespace detail

    // Public wrapper
    inline std::string ReflowCjkParagraphs(const std::string &utf8Text,
                                           const bool addPdfPageHeader,
                                           const bool compact) {
        return detail::ReflowCjkParagraphs(utf8Text, addPdfPageHeader, compact);
    }
} // namespace pdfium
