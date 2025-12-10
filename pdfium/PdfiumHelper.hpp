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

        // CJK punctuation / sentence enders
        static const std::u32string CJK_PUNCT_END =
                U"。！？；：…—”」’』）】》〗〔〕〉］｝.!?";

        // Dialog brackets (openers + closers)
        // Note: we intentionally keep dialog brackets out of OPEN/CLOSE_BRACKETS
        // so that heading-bracket logic does not conflict with dialog detection.
        static const std::u32string DIALOG_OPENERS = U"“‘「『﹁﹃";
        static const std::u32string DIALOG_CLOSERS = U"”’」』﹂﹄";

        // Brackets used for heading unmatched-bracket checks
        static const std::u32string OPEN_BRACKETS = U"（([【《<{";
        static const std::u32string CLOSE_BRACKETS = U"）)]】》>}";

        // Title heading keywords (equivalent to TITLE_HEADING_REGEX words)
        static const std::u32string TITLE_WORDS[] = {
            U"前言", U"序章", U"终章", U"尾声", U"后记",
            U"番外", U"尾聲", U"後記"
        };

        // Chapter markers for patterns like "第…章 / 節 / 部 / 卷 / 回"
        static const std::u32string CHAPTER_MARKERS = U"章节部卷節回";

        // Closing bracket chars for chapter-ending rule
        static const std::u32string CHAPTER_END_BRACKETS = U"】》〗〕〉」』）";

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

            // 7. Publishing cycle
            U"發行日", U"发行日",
            U"初版",

            // 8. Common key without variants
            U"ISBN"
        };

        // ------------------------- Small utility helpers -------------------------

        inline bool Contains(const std::u32string &s, const char32_t ch) {
            return std::find(s.begin(), s.end(), ch) != s.end();
        }

        inline bool AnyOf(const std::u32string &s, const std::u32string &set) {
            return std::any_of(
                s.begin(), s.end(),
                [&](char32_t ch) { return Contains(set, ch); });
        }

        inline std::u32string RStrip(const std::u32string &s) {
            std::size_t end = s.size();
            while (end > 0) {
                char32_t ch = s[end - 1];
                if (ch == U' ' || ch == U'\t' || ch == U'\r')
                    --end;
                else
                    break;
            }
            return s.substr(0, end);
        }

        // Strip only half-width leading spaces, but keep full-width \u3000 indent.
        inline std::u32string StripHalfWidthIndentKeepFullWidth(const std::u32string &s) {
            std::size_t pos = 0;
            while (pos < s.size() && s[pos] == U' ')
                ++pos;
            return s.substr(pos);
        }

        // Trim spaces + full-width spaces from the left (for logical probes).
        inline std::u32string LStripSpacesAndFullWidth(const std::u32string &s) {
            std::size_t pos = 0;
            while (pos < s.size() &&
                   (s[pos] == U' ' || s[pos] == U'\t' || s[pos] == U'\u3000')) {
                ++pos;
            }
            return s.substr(pos);
        }

        inline std::u32string Strip(const std::u32string &s) {
            return RStrip(LStripSpacesAndFullWidth(s));
        }

        inline bool IsWhitespaceLine(const std::u32string &s) {
            return std::all_of(
                s.begin(), s.end(),
                [](char32_t ch) {
                    return ch == U' ' || ch == U'\t' || ch == U'\r' || ch == U'\n';
                });
        }

        inline bool BufferEndsWithCjkPunct(const std::u32string &s) {
            for (auto it = s.rbegin(); it != s.rend(); ++it) {
                char32_t ch = *it;
                if (ch == U' ' || ch == U'\t' || ch == U'\r' || ch == U'\n')
                    continue;
                return Contains(CJK_PUNCT_END, ch);
            }
            return false;
        }

        inline bool IsPageMarker(const std::u32string &s) {
            if (s.size() < 7) return false; // "=== x ==="
            if (s.rfind(U"=== ", 0) != 0)
                return false;
            std::size_t n = s.size();
            return n >= 3 && s[n - 1] == U'=' && s[n - 2] == U'=' && s[n - 3] == U'=';
        }

        // ------------------------- Metadata detection -------------------------

        /// Detect lines like:
        ///   書名：假面遊戲
        ///   作者 : 東野圭吾
        ///   出版時間　2024-03-12
        ///   ISBN 9787573506078
        inline bool IsMetadataLine(const std::u32string &line) {
            std::u32string s = Strip(line);
            if (s.empty())
                return false;

            if (s.size() > 30)
                return false;

            // Find first separator (：, :, or full-width space)
            std::size_t sep_idx = std::u32string::npos;
            for (std::size_t i = 0; i < s.size(); ++i) {
                char32_t ch = s[i];
                if (Contains(METADATA_SEPARATORS, ch)) {
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
            std::u32string key = Strip(s.substr(0, sep_idx));
            if (key.empty())
                return false;

            if (!METADATA_KEYS.count(key))
                return false;

            // First non-space after separator
            std::size_t j = sep_idx + 1;
            while (j < s.size()) {
                char32_t ch = s[j];
                if (ch == U' ' || ch == U'\t' || ch == U'\u3000')
                    ++j;
                else
                    break;
            }

            if (j >= s.size())
                return false;

            char32_t first_after = s[j];
            // If the value starts with dialog opener, it's more like dialog, not metadata.
            if (Contains(DIALOG_OPENERS, first_after))
                return false;

            return true;
        }

        // ------------------------- Heading / title heuristics -------------------------

        inline bool IsTitleHeading(const std::u32string &s_left) {
            std::u32string s = Strip(s_left);
            if (s.empty())
                return false;

            if (s.size() > 60)
                return false;

            // Fixed words, e.g. 前言 / 序章 / 終章 / 尾聲 / 後記 / 番外
            for (const auto &w: TITLE_WORDS) {
                if (s.rfind(w, 0) == 0)
                    return true;
            }

            // Pattern: "第...章/节/部/卷/節/回" within first ~12 chars
            if (s[0] == U'第') {
                for (std::size_t i = 1; i < s.size(); ++i) {
                    char32_t ch = s[i];
                    if (Contains(CHAPTER_MARKERS, ch)) {
                        return i <= 12;
                    }
                    if (i > 12)
                        return false;
                }
            }

            return false;
        }

        inline bool HasOpenBracketNoClose(const std::u32string &s) {
            bool hasOpen = AnyOf(s, OPEN_BRACKETS);
            if (!hasOpen) return false;
            if (AnyOf(s, CLOSE_BRACKETS)) return false;
            return true;
        }

        /// Heading-like heuristic for short CJK titles / emphasis lines
        inline bool IsHeadingLike(const std::u32string &raw) {
            std::u32string s = Strip(raw);
            if (s.empty())
                return false;

            // Keep page markers intact
            if (IsPageMarker(s))
                return false;

            // If ends with CJK end punctuation → not heading
            char32_t last = s.back();
            if (Contains(CJK_PUNCT_END, last))
                return false;

            // Reject any short line that contains comma-like punctuation
            if (AnyOf(s, U"，,、"))
                return false;

            // Reject unclosed brackets
            if (HasOpenBracketNoClose(s))
                return false;

            std::size_t len = s.size();
            if (len <= 10) {
                // If a very short line contains ANY CJK end punctuation → not heading
                if (AnyOf(s, CJK_PUNCT_END))
                    return false;

                bool hasNonAscii = false;
                bool allAscii = true;
                bool hasLetter = false;
                bool allDigits = true;

                for (char32_t ch: s) {
                    if (ch > 0x7F) {
                        hasNonAscii = true;
                        allAscii = false;
                        allDigits = false;
                        continue;
                    }

                    if (!(ch >= U'0' && ch <= U'9')) {
                        allDigits = false;
                    }

                    if ((ch >= U'a' && ch <= U'z') || (ch >= U'A' && ch <= U'Z')) {
                        hasLetter = true;
                    }
                }

                // Rule C: pure ASCII digits → heading
                if (allDigits)
                    return true;

                // Rule A: short CJK/mixed line → heading
                if (hasNonAscii)
                    return true;

                // Rule B: short ASCII with at least one letter → heading
                if (allAscii && hasLetter)
                    return true;
            }

            return false;
        }

        // Chapter-like ending: line <= 15 chars and last non-bracket char in CHAPTER_MARKERS
        inline bool IsChapterEnding(const std::u32string &s_raw) {
            std::u32string s = Strip(s_raw);
            if (s.empty())
                return false;

            if (s.size() > 15)
                return false;

            std::size_t end = s.size();
            while (end > 0 && Contains(CHAPTER_END_BRACKETS, s[end - 1])) {
                --end;
            }
            if (end == 0)
                return false;

            char32_t last = s[end - 1];
            return Contains(CHAPTER_MARKERS, last);
        }

        // Dialog start: ignore half/full-width spaces, then check if first char is dialog opener
        inline bool IsDialogStart(const std::u32string &line) {
            std::u32string s = LStripSpacesAndFullWidth(line);
            if (s.empty())
                return false;
            return Contains(DIALOG_OPENERS, s[0]);
        }

        // ------------------------- Collapse repeated segments -------------------------

        // Collapse repeated substring inside a token:
        //   - token length 4..200
        //   - base unit length 4..10
        //   - repeated at least 3 times exactly
        inline std::u32string CollapseRepeatedToken(const std::u32string &token) {
            const std::size_t length = token.size();
            if (length < 4 || length > 200)
                return token;

            for (std::size_t unit_len = 4; unit_len <= 10; ++unit_len) {
                if (unit_len > length / 3)
                    break;
                if (length % unit_len != 0)
                    continue;

                const std::u32string unit = token.substr(0, unit_len);
                const std::size_t repeat_count = length / unit_len;

                bool allMatch = true;
                for (std::size_t i = 1; i < repeat_count; ++i) {
                    std::size_t start = i * unit_len;
                    if (token.compare(start, unit_len, unit) != 0) {
                        allMatch = false;
                        break;
                    }
                }

                if (allMatch)
                    return unit;
            }

            return token;
        }

        // Phrase-level collapse: collapse repeated word sequences
        inline std::vector<std::u32string>
        CollapseRepeatedWordSequences(const std::vector<std::u32string> &parts) {
            const std::size_t n = parts.size();
            constexpr std::size_t MIN_REPEATS = 3;
            constexpr std::size_t MAX_PHRASE_LEN = 8;

            if (n < MIN_REPEATS)
                return parts;

            for (std::size_t start = 0; start < n; ++start) {
                for (std::size_t phrase_len = 1;
                     phrase_len <= MAX_PHRASE_LEN && start + phrase_len <= n;
                     ++phrase_len) {
                    std::size_t count = 1;

                    while (true) {
                        std::size_t next_start = start + count * phrase_len;
                        if (next_start + phrase_len > n)
                            break;

                        bool equal = true;
                        for (std::size_t k = 0; k < phrase_len; ++k) {
                            if (parts[start + k] != parts[next_start + k]) {
                                equal = false;
                                break;
                            }
                        }

                        if (!equal)
                            break;

                        ++count;
                    }

                    if (count >= MIN_REPEATS) {
                        std::vector<std::u32string> result;
                        result.reserve(n - (count - 1) * phrase_len);

                        // prefix
                        for (std::size_t i = 0; i < start; ++i)
                            result.push_back(parts[i]);

                        // one copy of the phrase
                        for (std::size_t k = 0; k < phrase_len; ++k)
                            result.push_back(parts[start + k]);

                        // tail
                        std::size_t tail_start = start + count * phrase_len;
                        for (std::size_t i = tail_start; i < n; ++i)
                            result.push_back(parts[i]);

                        return result;
                    }
                }
            }

            return parts;
        }

        inline std::u32string CollapseRepeatedSegments(const std::u32string &line) {
            std::u32string trimmed = Strip(line);
            if (trimmed.empty())
                return line;

            // Split on spaces/tabs
            std::vector<std::u32string> parts;
            std::u32string current;
            for (char32_t ch: trimmed) {
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

            // 1) Phrase-level collapse
            auto phrase_collapsed = CollapseRepeatedWordSequences(parts);

            // 2) Token-level collapse
            std::u32string out;
            bool first = true;
            for (auto &tok: phrase_collapsed) {
                auto collapsed = CollapseRepeatedToken(tok);
                if (!first)
                    out.push_back(U' ');
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
                for (char32_t ch: s) {
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

            bool is_unclosed() const {
                return double_quote > 0 ||
                       single_quote > 0 ||
                       corner > 0 ||
                       corner_bold > 0 ||
                       corner_top > 0 ||
                       corner_wide > 0;
            }
        };

        // ------------------------- Core reflow implementation -------------------------

        inline std::string ReflowCjkParagraphs(const std::string &utf8Text,
                                               bool addPdfPageHeader,
                                               bool compact) {
            // Whole string is whitespace → return as-is
            if (std::all_of(
                utf8Text.begin(),
                utf8Text.end(),
                [](char ch) {
                    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
                })) {
                return utf8Text;
            }

            // Normalize CRLF / CR → LF
            std::string norm;
            norm.reserve(utf8Text.size());
            for (std::size_t i = 0; i < utf8Text.size(); ++i) {
                char c = utf8Text[i];
                if (c == '\r') {
                    if (i + 1 < utf8Text.size() && utf8Text[i + 1] == '\n') {
                        // Skip CR of CRLF, LF will be processed next
                        continue;
                    }
                    norm.push_back('\n');
                } else {
                    norm.push_back(c);
                }
            }

            // Split into UTF-8 lines, then convert to UTF-32
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
                lines32.push_back(Utf8ToU32(current));
            }

            std::vector<std::u32string> segments;
            std::u32string buffer;
            DialogState dialog_state;

            auto flush_buffer_and_push = [&](const std::u32string &line) {
                if (!buffer.empty()) {
                    segments.push_back(buffer);
                    buffer.clear();
                    dialog_state.reset();
                }
                segments.push_back(line);
            };

            for (const auto &raw_line32: lines32) {
                // 1) Visual form: trim right + strip only half-width leading spaces
                std::u32string stripped = RStrip(raw_line32);
                stripped = StripHalfWidthIndentKeepFullWidth(stripped);

                // 2) Collapse style-layer repetition (per line)
                std::u32string line_text = CollapseRepeatedSegments(stripped);

                // 3) Logical probe for headings: no indent at all
                std::u32string heading_probe = LStripSpacesAndFullWidth(line_text);

                // 4) Empty line handling
                if (IsWhitespaceLine(heading_probe)) {
                    if (!addPdfPageHeader && !buffer.empty()) {
                        if (!BufferEndsWithCjkPunct(buffer)) {
                            // Looks like a layout gap / fake page break → skip
                            continue;
                        }
                    }

                    // End of paragraph: flush buffer, do NOT add empty segment
                    if (!buffer.empty()) {
                        segments.push_back(buffer);
                        buffer.clear();
                        dialog_state.reset();
                    }
                    continue;
                }

                // 5) Page markers: "=== [Page x/y] ==="
                if (IsPageMarker(heading_probe)) {
                    flush_buffer_and_push(line_text);
                    continue;
                }

                // 6) Heading / metadata detection
                bool is_title_heading = IsTitleHeading(heading_probe);
                bool is_short_heading = IsHeadingLike(line_text);
                bool is_metadata = IsMetadataLine(line_text);

                // 6a) Metadata lines as standalone segments
                if (is_metadata) {
                    flush_buffer_and_push(line_text);
                    continue;
                }

                // 6b) Strong title headings as standalone segments
                if (is_title_heading) {
                    flush_buffer_and_push(line_text);
                    continue;
                }

                // 6c) Soft heading-like lines
                if (is_short_heading) {
                    if (!buffer.empty()) {
                        std::u32string bt = RStrip(buffer);
                        if (!bt.empty()) {
                            char32_t last = bt.back();
                            if (last == U'，' || last == U',') {
                                // Comma-ending previous line → treat as continuation
                                // fall through below
                            } else {
                                // Real heading boundary → split
                                flush_buffer_and_push(line_text);
                                continue;
                            }
                        } else {
                            // Buffer only whitespace → treat as standalone heading
                            segments.push_back(line_text);
                            continue;
                        }
                    } else {
                        // No previous text → standalone heading
                        segments.push_back(line_text);
                        continue;
                    }
                }

                // 7) Dialog start detection
                bool current_is_dialog_start = IsDialogStart(line_text);

                if (buffer.empty()) {
                    buffer = line_text;
                    dialog_state.reset();
                    dialog_state.update(line_text);
                    continue;
                }

                // NEW: if previous line ends with comma, do NOT force flush even if
                // this line starts with a dialog opener.
                if (current_is_dialog_start) {
                    std::u32string trimmed_buf = RStrip(buffer);
                    if (!trimmed_buf.empty()) {
                        char32_t last = trimmed_buf.back();
                        if (last != U'，' && last != U',') {
                            // Safe to flush previous paragraph and start new dialog block
                            flush_buffer_and_push(line_text);
                            continue;
                        }
                        // else: fall through, treat as continuation
                    } else {
                        // buffer only whitespace, just treat as new dialog block
                        flush_buffer_and_push(line_text);
                        continue;
                    }
                }

                // Colon + dialog continuation:
                //   "她寫了一行字：" + "  「如果連自己都不相信……」"
                if (!buffer.empty()) {
                    std::u32string trimmed_buf = RStrip(buffer);
                    if (!trimmed_buf.empty()) {
                        char32_t last = trimmed_buf.back();
                        if (last == U'：' || last == U':') {
                            std::u32string after_indent = LStripSpacesAndFullWidth(line_text);
                            if (!after_indent.empty() &&
                                Contains(DIALOG_OPENERS, after_indent[0])) {
                                buffer += line_text;
                                dialog_state.update(line_text);
                                continue;
                            }
                        }
                    }
                }

                // CJK punctuation boundary: if buffer ends with CJK punctuation
                // and dialog is balanced, treat as paragraph break.
                if (!buffer.empty() &&
                    BufferEndsWithCjkPunct(buffer) &&
                    !dialog_state.is_unclosed()) {
                    flush_buffer_and_push(line_text);
                    continue;
                }

                // Chapter-like ending
                if (IsChapterEnding(buffer)) {
                    flush_buffer_and_push(line_text);
                    continue;
                }

                // Default: soft join
                buffer += line_text;
                dialog_state.update(line_text);
            }

            // Flush last buffer
            if (!buffer.empty()) {
                segments.push_back(buffer);
            }

            // Join segments with one or two newlines
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

    // Public wrapper (API remains the same)
    inline std::string ReflowCjkParagraphs(const std::string &utf8Text,
                                           const bool addPdfPageHeader,
                                           const bool compact) {
        return detail::ReflowCjkParagraphs(utf8Text, addPdfPageHeader, compact);
    }
} // namespace pdfium
