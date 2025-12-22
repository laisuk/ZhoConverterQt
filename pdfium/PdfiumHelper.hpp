#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_set>
#include <vector>
#include <algorithm>


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

        inline void NormalizeNewlinesInPlace(std::string &s) {
            std::string out;
            out.reserve(s.size());

            for (size_t i = 0; i < s.size(); ++i) {
                if (const char c = s[i]; c == '\r') {
                    // skip CR, but convert CRLF / CR to LF
                    if (i + 1 < s.size() && s[i + 1] == '\n')
                        ++i;
                    out.push_back('\n');
                } else {
                    out.push_back(c);
                }
            }

            s.swap(out);
        }

        inline std::string TrimCopy(const std::string &s) {
            size_t start = 0;
            size_t end = s.size();

            while (start < end && std::isspace(static_cast<unsigned char>(s[start])))
                ++start;
            while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
                --end;

            return s.substr(start, end - start);
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
            std::string pageText = detail::ExtractPageText(page.Get());
            detail::NormalizeNewlinesInPlace(pageText); // \r\n and \r -> \n
            result += detail::TrimCopy(pageText);
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
                                  // throw;
                                  return "";
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
        static const std::u32string CJK_PUNCT_END = U"。！？；：…—”」’』）】》〗〕〉］｝.!?)";

        // Dialog brackets (from DIALOG_OPEN_TO_CLOSE)
        static const std::u32string DIALOG_OPENERS = U"“‘「『";
        static const std::u32string DIALOG_CLOSERS = U"”’」』";

        // Brackets for heading check
        static const std::u32string OPEN_BRACKETS = U"（([【《〈［｛";
        static const std::u32string CLOSE_BRACKETS = U"）)]】》〉］｝";

        // Title heading patterns (see TITLE_HEADING_REGEX)
        static const std::u32string TITLE_WORDS[] = {
            U"前言", U"序章", U"终章", U"尾声", U"后记",
            U"番外", U"尾聲", U"後記", U"楔子"
        };

        // Markers like 章 / 节 / 部 / 卷 / 回 etc.
        static const std::u32string CHAPTER_MARKERS = U"章节部卷節回";

        // Characters that invalidate chapter headings when they appear *immediately after*
        // a chapter marker, matching the C# regex [章节部卷節回][^分合]
        //
        // Later if you find more patterns, simply append to this string.
        // Example future additions:
        //
        //   "U"分合附补增修編編輯"   // ← expandable
        //
        static const std::u32string EXCLUDED_CHAPTER_MARKERS_PREFIX = U"分合";

        // Closing bracket chars for chapter-ending rule
        static const std::u32string CHAPTER_END_BRACKETS = U"】》〗〕〉」』）］";

        static constexpr std::size_t SHORT_HEADING_MAX_LEN = 8;

        // Metadata separators: full-width colon, ASCII colon, ideographic space
        static const std::u32string METADATA_SEPARATORS = U"：:　";

        // Metadata keys (書名 / 作者 / 出版時間 / 版權 / ISBN / etc.)
        static const std::unordered_set<std::u32string> METADATA_KEYS = {
            // 1. Title / Author / Publishing
            U"書名", U"书名",
            U"作者",
            U"譯者", U"译者",
            U"校訂", U"校订",
            U"出版社",
            U"出版時間", U"出版时间",
            U"出版日期",

            // 2. Copyright / License
            U"版權", U"版权",
            U"版權頁", U"版权页",
            U"版權信息", U"版权信息",

            // 3. Editor / Pricing
            U"責任編輯", U"责任编辑",
            U"編輯", U"编辑",
            U"責編", U"责编",
            U"定價", U"定价",

            // 4. Descriptions / Forewords (only some are treated as metadata)
            U"簡介", U"简介",
            U"前言",
            U"序章",
            U"終章", U"终章",
            U"尾聲", U"尾声",
            U"後記", U"后记",

            // 5. Digital publishing (ebook platforms)
            U"品牌方",
            U"出品方",
            U"授權方", U"授权方",
            U"電子版權", U"数字版权",
            U"掃描", U"扫描",
            U"OCR",

            // 6. CIP / Cataloging
            U"CIP",
            U"在版編目", U"在版编目",
            U"分類號", U"分类号",
            U"主題詞", U"主题词",
            U"類型", U"类型",
            U"系列",

            // 7. Publishing cycle
            U"發行日", U"发行日",
            U"初版",

            // 8. Common key without variants
            U"ISBN"
        };

        // ------------------------- Small utility helpers -------------------------

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
            return std::any_of(s.begin(), s.end(),
                               [&](const char32_t ch) { return Contains(set, ch); });
        }

        // Contains any CJK (very simple heuristic: >0x7F)
        inline bool ContainsCjk(const std::u32string &s) {
            return std::any_of(s.begin(), s.end(),
                               [](const char32_t ch) { return ch > 0x7F; });
        }


        inline bool IsAscii(const char32_t ch) {
            return ch <= 0x7F;
        }

        // All ASCII?
        inline bool IsAllAscii(const std::u32string &s) {
            return std::all_of(s.begin(), s.end(), IsAscii);
        }

        // Any A-Z / a-z
        inline bool HasLatinAlpha(const std::u32string &s) {
            return std::any_of(s.begin(), s.end(), [](const char32_t ch) {
                return (ch >= U'a' && ch <= U'z') || (ch >= U'A' && ch <= U'Z');
            });
        }

        // Minimal CJK checker (BMP focused)
        // Matches C# logic exactly
        inline bool IsCjk(const char32_t ch) {
            const auto c = static_cast<uint32_t>(ch);

            // CJK Unified Ideographs Extension A: U+3400–U+4DBF
            if ((c - 0x3400u) <= (0x4DBFu - 0x3400u))
                return true;

            // CJK Unified Ideographs: U+4E00–U+9FFF
            if ((c - 0x4E00u) <= (0x9FFFu - 0x4E00u))
                return true;

            // CJK Compatibility Ideographs: U+F900–U+FAFF
            return (c - 0xF900u) <= (0xFAFFu - 0xF900u);
        }


        // ASCII letter/digit?
        inline bool IsAsciiLetterOrDigit(const char32_t ch) {
            return (ch >= U'0' && ch <= U'9') ||
                   (ch >= U'a' && ch <= U'z') ||
                   (ch >= U'A' && ch <= U'Z');
        }

        // Full-width digits: '０'..'９'
        inline bool IsFullwidthDigit(const char32_t ch) {
            return (ch >= U'０' && ch <= U'９');
        }

        // Neutral ASCII separators allowed (do not count as ASCII content)
        inline bool IsNeutralAsciiForMixed(const char32_t ch) {
            return ch == U' ' || ch == U'-' || ch == U'/' || ch == U':' || ch == U'.';
        }

        // Mixed CJK + ASCII (like "第3章 Chapter 1", "iPhone 16 Pro Max", etc.)
        // Rules (same as your C#):
        // - Allow neutral ASCII separators: space - / : .
        // - ASCII must be alnum only; other ASCII punctuation rejects.
        // - Allow full-width digits as ASCII content.
        // - Non-ASCII must be CJK (IsCjk), otherwise reject.
        // - Return true only if both CJK and ASCII content appear.
        inline bool IsMixedCjkAscii(const std::u32string &s) {
            bool hasCjk = false;
            bool hasAscii = false;

            for (const char32_t ch: s) {
                // Neutral ASCII (allowed, but doesn't count as ASCII content)
                if (IsNeutralAsciiForMixed(ch))
                    continue;

                if (ch <= 0x7F) {
                    if (IsAsciiLetterOrDigit(ch)) {
                        hasAscii = true;
                    } else {
                        return false; // other ASCII punct/control => reject
                    }
                } else if (IsFullwidthDigit(ch)) {
                    hasAscii = true;
                } else if (IsCjk(ch)) {
                    hasCjk = true;
                } else {
                    return false; // non-ASCII, non-CJK => reject
                }

                if (hasCjk && hasAscii)
                    return true;
            }

            return false;
        }

        // ------------------------------------------------------------
        // Style-layer repeat collapse for PDF headings / title lines.
        //
        // Conceptually similar to:
        //
        //    (.{4,10}?)\1{2,3}
        //
        // i.e. “a phrase of length 4–10 chars, repeated 3–4 times”,
        // but implemented in a token- and phrase-aware way so we can
        // correctly handle CJK titles and multi-word headings.
        //
        // This routine is intentionally conservative:
        //   - It targets layout / styling noise (highlighted titles,
        //     duplicated TOC entries, etc.).
        //   - It avoids collapsing natural language like “哈哈哈哈哈哈”.
        // ------------------------------------------------------------

        // Token-level: collapse a single token if it is entirely made of
        // a repeated substring of length 4..10, repeated at least 3 times.
        inline std::u32string CollapseRepeatedToken(const std::u32string &token) {
            const std::size_t length = token.size();
            // Very short tokens or huge ones are unlikely to be styled repeats.
            if (length < 4 || length > 200) {
                return token;
            }

            // Try unit sizes between 4 and 10 chars, and require at least
            // 3 repeats (N >= 3). This corresponds roughly to:
            //
            //   (.{4,10}?)\1{2,}
            //
            // but constrained to exactly fill the entire token.
            for (std::size_t unit_len = 4;
                 unit_len <= 10 && unit_len <= length / 3;
                 ++unit_len) {
                if (length % unit_len != 0)
                    continue;

                const std::u32string unit = token.substr(0, unit_len);
                bool all_match = true;

                for (std::size_t pos = 0; pos < length; pos += unit_len) {
                    if (token.compare(pos, unit_len, unit) != 0) {
                        all_match = false;
                        break;
                    }
                }

                if (all_match) {
                    // Token is just [unit] repeated N times (N >= 3):
                    // collapse it to a single unit.
                    return unit;
                }
            }

            return token;
        }

        // --------------------------------
        // Normalize Repeated Words
        // --------------------------------

        // Phrase-level: collapse repeated sequences of tokens (phrases).
        //
        // Example:
        //   「背负着一切的麒麟 背负着一切的麒麟 背负着一切的麒麟 背负着一切的麒麟」
        //   → 「背负着一切的麒麟」
        inline std::vector<std::u32string>
        CollapseRepeatedWordSequences(const std::vector<std::u32string> &parts) {
            constexpr int minRepeats = 3; // minimum number of repeats
            constexpr int maxPhraseLen = 8; // typical heading phrases are short

            const std::size_t n = parts.size();
            if (n < static_cast<std::size_t>(minRepeats)) {
                return parts;
            }

            // Scan from left to right for any repeating phrase.
            for (std::size_t start = 0; start < n; ++start) {
                for (int phraseLen = 1;
                     phraseLen <= maxPhraseLen && start + static_cast<std::size_t>(phraseLen) <= n;
                     ++phraseLen) {
                    std::size_t count = 1;

                    while (true) {
                        const std::size_t nextStart = start + count * static_cast<std::size_t>(phraseLen);
                        if (nextStart + static_cast<std::size_t>(phraseLen) > n) {
                            break;
                        }

                        bool equal = true;
                        for (int k = 0; k < phraseLen; ++k) {
                            if (parts[start + k] != parts[nextStart + k]) {
                                equal = false;
                                break;
                            }
                        }

                        if (!equal)
                            break;

                        ++count;
                    }

                    if (count < static_cast<std::size_t>(minRepeats)) {
                        continue;
                    }

                    // Build collapsed list:
                    //   [prefix] + [one phrase] + [tail]
                    std::vector<std::u32string> result;
                    result.reserve(n - (count - 1) * static_cast<std::size_t>(phraseLen));

                    // Prefix before the repeated phrase.
                    for (std::size_t i = 0; i < start; ++i) {
                        result.push_back(parts[i]);
                    }

                    // Single copy of the repeated phrase.
                    for (int k = 0; k < phraseLen; ++k) {
                        result.push_back(parts[start + k]);
                    }

                    // Tail after all repeats.
                    const std::size_t tailStart = start + count * static_cast<std::size_t>(phraseLen);
                    for (std::size_t i = tailStart; i < n; ++i) {
                        result.push_back(parts[i]);
                    }

                    return result;
                }
            }

            return parts;
        }

        // Line-level wrapper:
        //   1) split on spaces/tabs into tokens
        //   2) collapse repeated *phrases*
        //   3) collapse repeated patterns inside each token
        inline std::u32string CollapseRepeatedSegments(const std::u32string &line) {
            if (line.empty())
                return line;

            // Split on spaces/tabs into discrete tokens.
            std::vector<std::u32string> parts; {
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
                if (!current.empty()) {
                    parts.push_back(current);
                }
            }

            if (parts.empty())
                return line;

            // 1) Phrase-level collapse
            parts = CollapseRepeatedWordSequences(parts);

            // 2) Token-level collapse
            std::u32string out;
            bool first = true;
            for (auto &tok: parts) {
                const std::u32string collapsed = CollapseRepeatedToken(tok);
                if (!first) {
                    out.push_back(U' ');
                }
                out += collapsed;
                first = false;
            }

            return out;
        }

        // ------------------------- DialogState -------------------------

        struct DialogState {
            int double_quote = 0; // “ ”
            int single_quote = 0; // ‘ ’
            int corner = 0; // 「 」
            int corner_bold = 0; // 『 』
            int corner_top = 0; // ﹁ ﹂
            int corner_wide = 0; // ﹄ ﹃

            void reset() {
                double_quote = 0;
                single_quote = 0;
                corner = 0;
                corner_bold = 0;
                corner_top = 0;
                corner_wide = 0;
            }

            void update(const std::u32string &s) {
                for (const char32_t ch: s) {
                    switch (ch) {
                        case U'“': ++double_quote;
                            break;
                        case U'”': if (double_quote > 0) --double_quote;
                            break;
                        case U'‘': ++single_quote;
                            break;
                        case U'’': if (single_quote > 0) --single_quote;
                            break;
                        case U'「': ++corner;
                            break;
                        case U'」': if (corner > 0) --corner;
                            break;
                        case U'『': ++corner_bold;
                            break;
                        case U'』': if (corner_bold > 0) --corner_bold;
                            break;
                        case U'﹁': ++corner_top;
                            break;
                        case U'﹂': if (corner_top > 0) --corner_top;
                            break;
                        case U'﹃': ++corner_wide;
                            break;
                        case U'﹄': if (corner_wide > 0) --corner_wide;
                            break;
                        default: break;
                    }
                }
            }

            [[nodiscard]] bool is_unclosed() const {
                return double_quote > 0 ||
                       single_quote > 0 ||
                       corner > 0 ||
                       corner_bold > 0 ||
                       corner_top > 0 ||
                       corner_wide > 0;
            }
        };

        // ------------------------- Metadata detection -------------------------
        /// Detect lines like:
        ///   書名：假面遊戲
        ///   作者 : 東野圭吾
        ///   出版時間　2024-03-12
        ///   ISBN 9787573506078
        inline bool IsMetadataLine(const std::u32string &line) {
            const std::u32string s = Strip(line);
            if (s.empty())
                return false;

            if (s.size() > 30)
                return false;

            // Find first separator (：, :, or full-width space)
            std::size_t sep_idx = std::u32string::npos;
            for (std::size_t i = 0; i < s.size(); ++i) {
                if (const char32_t ch = s[i]; Contains(METADATA_SEPARATORS, ch)) {
                    if (i == 0 || i > 10) {
                        // Separator too early or too far → not a compact key
                        return false;
                    }
                    sep_idx = i;
                    break;
                }
            }

            if (sep_idx == std::u32string::npos)
                return false;

            // Key before separator
            const std::u32string key = Strip(s.substr(0, sep_idx));
            if (key.empty())
                return false;
            if (!METADATA_KEYS.count(key))
                return false;

            // Find first non-space after the separator
            std::size_t j = sep_idx + 1;
            while (j < s.size()) {
                // ASCII space, tab, or full-width space
                if (const char32_t c = s[j]; c == U' ' || c == U'\t' || c == U'　') {
                    ++j;
                } else {
                    break;
                }
            }

            if (j >= s.size())
                return false;

            // If the value starts with dialog opener, it's more like dialog, not metadata.
            if (const char32_t first_after = s[j]; Contains(DIALOG_OPENERS, first_after))
                return false;

            return true;
        }

        // ------------------------- Title & heading heuristics -------------------------
        //
        // Matches:
        //  - 前言 / 序章 / 终章 / 尾声 / 后记 / 尾聲 / 後記
        //  - 番外 + optional short suffix
        //  - Short chapter-like lines with 第N章/卷/节/部/回 (excluding 分 / 合)
        //
        // Equivalent to:
        // ^(?=.{0,50}$)
        // (前言|序章|终章|尾声|后记|尾聲|後記|番外.{0,15}|.{0,10}?第.{0,5}?([章节部卷節回][^分合]).{0,20}?)
        //
        inline bool IsTitleHeading(const std::u32string &s_left) {
            const std::size_t len = s_left.size();
            if (len == 0 || len > 50)
                return false;

            // 1) Fixed title words
            for (const auto &w: TITLE_WORDS) {
                if (s_left.rfind(w, 0) == 0) {
                    return true;
                }
            }

            // 1b) 番外.{0,15}
            if (s_left.rfind(U"番外", 0) == 0) {
                // allow short suffix after 番外
                if (len <= 2 + 15)
                    return true;
            }

            // 2) Chapter-like: .{0,10}?第.{0,5}?([章节部卷節回][^分合]).{0,20}?
            //    Step-by-step scan, same semantics as regex but safer/debuggable.

            // 2a) Search for '第' within first 10 chars
            std::size_t di = std::u32string::npos;
            const std::size_t max_before_di = std::min<std::size_t>(10, len - 1);
            for (std::size_t i = 0; i <= max_before_di; ++i) {
                if (s_left[i] == U'第') {
                    di = i;
                    break;
                }
            }
            if (di == std::u32string::npos)
                return false;

            // 2b) After '第', scan up to 5 chars to find a chapter marker
            const std::size_t max_marker_pos = std::min<std::size_t>(len - 1, di + 1 + 5);
            for (std::size_t j = di + 1; j <= max_marker_pos; ++j) {
                if (const char32_t ch = s_left[j]; Contains(CHAPTER_MARKERS, ch)) {
                    // Next char must NOT be 分 / 合
                    if (j + 1 < len) {
                        if (const char32_t next = s_left[j + 1]; Contains(EXCLUDED_CHAPTER_MARKERS_PREFIX, next))
                            continue;
                    }

                    // Remaining tail length <= 20
                    if (len - j - 1 <= 20)
                        return true;
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
            // Unified spec with C#/Java/Python/Rust
            const std::u32string s = Strip(raw);
            if (s.empty()) return false;

            // Keep page markers intact
            if (IsPageMarker(s)) return false;

            // Last char cannot be terminal punctuation
            const char32_t last = s.back();
            if (Contains(CJK_PUNCT_END, last)) {
                return false;
            }

            // Cannot have an open bracket without a matching closed one
            if (HasOpenBracketNoClose(s)) {
                return false;
            }

            // Determine dynamic max length:
            //   - CJK/mixed: SHORT_HEADING_MAX_LEN
            //   - pure ASCII: double
            const bool all_ascii = IsAllAscii(s);
            const std::size_t max_len =
                    all_ascii || IsMixedCjkAscii(s) ? (SHORT_HEADING_MAX_LEN * 2) : SHORT_HEADING_MAX_LEN;

            if (const std::size_t len = s.size(); len > max_len) {
                return false;
            }

            // Analyze characters
            bool hasNonAscii = false;
            bool hasLetter = false;
            bool allAsciiDigits = true;

            for (const char32_t ch: s) {
                if (ch > 0x7F) {
                    hasNonAscii = true;
                    allAsciiDigits = false;
                    continue;
                }

                // Check ASCII digits
                if (!(ch >= U'0' && ch <= U'9')) {
                    allAsciiDigits = false;
                }

                // Check ASCII letters
                if ((ch >= U'a' && ch <= U'z') || (ch >= U'A' && ch <= U'Z')) {
                    hasLetter = true;
                }
            }

            // ----------------- RULE SET -----------------

            // Rule C: pure ASCII digits → heading-like (e.g., "1", "02", "12")
            if (allAsciiDigits) {
                return true;
            }

            // Rule A: CJK/mixed short line → heading-like
            if (hasNonAscii && last != U'，' && last != U',') {
                return true;
            }

            // Rule B: pure ASCII short line with letters → heading-like
            if (all_ascii && hasLetter) {
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

        inline bool IsBoxDrawingLine(const std::u32string &s) {
            auto is_ws = [](const char32_t ch) {
                // Minimal whitespace set (extend if needed)
                return ch == U' ' || ch == U'\t' || ch == U'\n' || ch == U'\r' || ch == 0x3000;
            };

            // Quick reject: all whitespace
            bool any_non_ws = false;
            for (const char32_t ch: s) {
                if (!is_ws(ch)) {
                    any_non_ws = true;
                    break;
                }
            }
            if (!any_non_ws) return false;

            int total = 0;

            for (const char32_t ch: s) {
                if (is_ws(ch)) continue;
                ++total;

                if (ch >= 0x2500 && ch <= 0x257F) continue; // box drawing

                if (ch == U'-' || ch == U'=' || ch == U'_' || ch == U'~' || ch == 0xFF5E) continue; // incl '～'

                if (ch == U'*' || ch == 0xFF0A /*＊*/ || ch == 0x2605 /*★*/ || ch == 0x2606 /*☆*/) continue;

                return false;
            }

            return total >= 3;
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

                // NEW: style-layer repeat collapse applied at paragraph level.
                stripped = CollapseRepeatedSegments(stripped);

                // IMPORTANT: collapse may change leading layout; recompute probe
                stripped_left = LStrip(stripped);

                // 1) Empty line
                if (stripped.empty()) {
                    if (!addPdfPageHeader && !buffer.empty()) {
                        if (char32_t last_char = buffer.back(); !Contains(CJK_PUNCT_END, last_char)) {
                            // treat as layout gap, skip
                            continue;
                        }
                    }

                    flush_buffer();
                    continue;
                }

                // 2) Visual divider / box drawing line (MUST force paragraph break)
                if (IsBoxDrawingLine(stripped_left)) {
                    flush_buffer();

                    // keep divider as its own segment (u32)
                    segments.emplace_back(stripped_left);

                    // optional: blank line after divider
                    flush_buffer();
                    continue;
                }

                bool is_title_heading = IsTitleHeading(stripped_left);
                // *** NEW: metadata detection (書名：…, 作者：…, ISBN … etc.) ***
                bool is_metadata = IsMetadataLine(stripped_left);
                // NEW: weak heading-like detection on *current* line
                bool is_short_heading = IsHeadingLike(stripped);

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

                // 3a) Metadata lines (書名：…, 作者：…, ISBN …)
                //     These should be standalone segments, not joined to prose.
                if (is_metadata) {
                    flush_buffer();
                    segments.push_back(stripped);
                    continue;
                }

                // 3b) 弱 heading-like：要先看上一段是否逗號結尾
                if (is_short_heading) {
                    if (!buffer.empty()) {
                        // check last non-space char of buffer
                        if (std::u32string bt = RStrip(buffer); !bt.empty()) {
                            if (char32_t last = bt.back(); last == U'，' || last == U',' || last == U'、') {
                                // previous ends with comma → treat as continuation, NOT heading
                                // fall through; normal rules below will handle merge/split
                            } else {
                                // real heading → flush buffer, this line becomes its own segment
                                flush_buffer();
                                segments.push_back(stripped);
                                continue;
                            }
                        } else {
                            // buffer only whitespace → treat as heading
                            segments.push_back(stripped);
                            continue;
                        }
                    } else {
                        // no previous text → heading on its own
                        segments.push_back(stripped);
                        continue;
                    }
                }

                bool current_is_dialog_start = IsDialogStart(stripped);

                // 4) First line of new paragraph
                if (buffer.empty()) {
                    // First line – just start a new paragraph (dialog or not)
                    buffer = stripped;
                    dialog_state.reset();
                    dialog_state.update(stripped);
                    continue;
                }

                std::u32string &buffer_text = buffer;

                // We already have some text in buffer
                if (!buffer_text.empty()) {
                    // 🔸 NEW RULE: If previous line ends with comma,
                    //     do NOT flush even if this line starts dialog.
                    //     (comma-ending means the sentence is not finished)
                    std::u32string trimmed = RStrip(buffer_text);

                    if (char32_t last = trimmed.empty() ? U'\0' : trimmed.back();
                        last == U'，' || last == U',' || last == U'、') {
                        // fall through → treat as continuation
                        // do NOT flush here even if current_is_dialog_start
                    } else if (current_is_dialog_start) {
                        // *** DIALOG: if this line starts a dialog,
                        //     flush previous paragraph (only if safe)
                        flush_buffer();
                        buffer = stripped;
                        dialog_state.reset();
                        dialog_state.update(stripped);
                        continue;
                    }
                } else {
                    // buffer logically empty, just add new dialog line
                    if (current_is_dialog_start) {
                        buffer = stripped;
                        dialog_state.reset();
                        dialog_state.update(stripped);
                        continue;
                    }
                }

                // Colon + dialog continuation: "她写了一行字：" + "  「如果连自己都不相信……」"
                if (!buffer_text.empty()) {
                    if (char32_t last = buffer_text.back(); last == U'：' || last == U':') {
                        // ignore leading half/full-width spaces for dialog opener
                        if (std::u32string after_indent = LStrip(stripped); !after_indent.empty() && Contains(
                                                                                DIALOG_OPENERS, after_indent[0])) {
                            buffer += stripped;
                            dialog_state.update(stripped);
                            continue;
                        }
                    }
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
