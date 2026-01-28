#pragma once
//
// ReflowCommon.hpp
// -----------------------------------------------------------------------------
// Shared helpers for CJK paragraph reflow (UTF-8 in / UTF-8 out).
//
// This header intentionally contains ONLY reusable “common helpers”:
// - UTF-8 <-> UTF-32 conversion helpers
// - punctuation tables / character-class predicates
// - trimming / whitespace / token utilities
// - dialog/metadata/title heuristics
//
// Core reflow orchestration lives in ReflowHelper.hpp.
//
// Design note:
// Keep this file aligned with the C# helpers (PunctSets / CjkText) over time.
// -----------------------------------------------------------------------------

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>
#include "CjkText.hpp"
#include "PunctSets.hpp"

namespace pdfium::detail {
    // Punct sets
    using text::punct::CLAUSE_OR_END_PUNCT;
    using text::punct::DIALOG_OPENERS;
    using text::punct::DIALOG_CLOSERS;
    using text::punct::IsClauseOrEndPunct;
    using text::punct::IsStrongSentenceEnd;
    using text::punct::EndsWithStrongSentenceEnd;
    using text::punct::ContainsStrongSentenceEnd;
    using text::punct::IsBracketOpener;
    using text::punct::IsBracketCloser;
    using text::punct::IsMatchingBracket;
    using text::punct::IsWrappedByMatchingBracket;
    using text::punct::IsBracketTypeBalanced;
    using text::punct::HasUnclosedBracket;
    using text::punct::IsCommaLike;
    using text::punct::ContainsAnyCommaLike;
    using text::punct::IsColonLike;
    using text::punct::EndsWithColonLike;
    using text::punct::IsDialogCloser;
    using text::punct::IsQuoteCloser;
    using text::punct::BeginsWithDialogOpener;
    using text::punct::IsAllowedPostfixCloser;
    using text::punct::EndsWithAllowedPostfixCloser;

    // Text helpers
    using text::IsWhitespace;
    using text::TryGetLastNonWhitespace;
    using text::TryGetLastTwoNonWhitespace;
    using text::TryGetPrevNonWhitespace;
    using text::IsCjk;
    using text::IsAsciiDigit;
    using text::IsAsciiLetter;
    using text::IsAsciiLetterOrDigit;
    using text::IsAllAscii;
    using text::IsFullwidthDigit;
    using text::IsNeutralAsciiForMixed;
    using text::IsMixedCjkAscii;
    using text::IsMostlyCjk;
    using text::IsAllCjkIgnoringWhitespace;
    using text::ContainsAnyCjk;
    using text::EndsWithEllipsis;

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

    // Title heading patterns (see TITLE_HEADING_REGEX)
    inline constexpr std::u32string_view TITLE_WORDS[] = {
        U"前言", U"序章", U"终章", U"尾声", U"后记",
        U"番外", U"尾聲", U"後記", U"楔子"
    };

    // Markers like 章 / 节 / 部 / 卷 / 回 etc.
    inline constexpr std::u32string_view CHAPTER_MARKERS = U"章节部卷節回";

    // Characters that invalidate chapter headings when they appear *immediately after*
    // a chapter marker, matching the C# regex: [章节部卷節回][^分合的]
    //
    // Later if you find more patterns, simply append to this string.
    // Example future additions:
    //
    //   "U"分合附补增修編編輯"   // ← expandable
    //
    inline constexpr std::u32string_view EXCLUDED_CHAPTER_MARKERS_PREFIX = U"分合的";

    // For "(?:卷|章)[一二三四五六七八九十]"
    inline constexpr std::u32string_view CN_NUM_1_TO_10 = U"一二三四五六七八九十";

    // Closing bracket chars for chapter-ending rule
    inline constexpr std::u32string_view CHAPTER_END_BRACKETS = U"】》〗〕〉」』）］";

    static constexpr std::size_t SHORT_HEADING_MAX_LEN = 8;

    // Metadata separators: full-width colon, ASCII colon, ideographic space
    inline constexpr std::u32string_view METADATA_SEPARATORS = U"：:　";

    // ------------------------- Metadata keys (view-based, zero allocation) -------------------------

    inline constexpr std::u32string_view METADATA_KEYS[] = {
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

        // 4. Descriptions / Forewords
        U"簡介", U"简介",
        U"前言",
        U"序章",
        U"終章", U"终章",
        U"尾聲", U"尾声",
        U"後記", U"后记",

        // 5. Digital publishing
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

        // 8. Common
        U"ISBN"
    };

    [[nodiscard]]
    inline bool IsMetadataKey(std::u32string_view key) noexcept {
        return std::any_of(
            std::begin(METADATA_KEYS),
            std::end(METADATA_KEYS),
            [&](auto k) noexcept { return k == key; }
        );
    }

    // ------------------------- Small utility helpers -------------------------

    // Utility: startswith "=== " && endswith "==="
    // Uses view: no allocation.
    [[nodiscard]]
    inline bool IsPageMarker(const std::u32string_view s) noexcept {
        if (s.size() < 7) return false; // "=== x ==="
        return s.rfind(U"=== ", 0) == 0 &&
               s.size() >= 3 &&
               s[s.size() - 1] == U'=' &&
               s[s.size() - 2] == U'=' &&
               s[s.size() - 3] == U'=';
    }

    // Utility: contains (view-based)
    [[nodiscard]]
    inline bool Contains(const std::u32string_view s, const char32_t ch) noexcept {
        return std::find(s.begin(), s.end(), ch) != s.end();
    }

    // ------------------------- Trim helpers (view-based, no allocation) -------------------------

    [[nodiscard]]
    inline std::u32string_view RStripView(const std::u32string_view s) noexcept {
        std::size_t end = s.size();
        while (end > 0) {
            if (const char32_t ch = s[end - 1];
                ch == U' ' || ch == U'\t' || ch == U'\r' || ch == U'\n' || ch == U'\u3000'
            )
                --end;
            else
                break;
        }
        return s.substr(0, end);
    }

    [[nodiscard]]
    inline std::u32string_view LStripView(const std::u32string_view s) noexcept {
        std::size_t pos = 0;
        while (pos < s.size()) {
            if (const char32_t ch = s[pos];
                ch == U' ' || ch == U'\t' || ch == U'\r' || ch == U'\n' || ch == U'\u3000'
            )
                ++pos;
            else
                break;
        }
        return s.substr(pos);
    }

    [[nodiscard]]
    inline std::u32string_view StripView(const std::u32string_view s) noexcept {
        return RStripView(LStripView(s));
    }

    // If you still want to allocate versions, keep them as wrappers:
    [[nodiscard]]
    inline std::u32string RStrip(const std::u32string_view s) { return std::u32string(RStripView(s)); }

    [[nodiscard]]
    inline std::u32string LStrip(const std::u32string_view s) { return std::u32string(LStripView(s)); }

    [[nodiscard]]
    inline std::u32string Strip(const std::u32string_view s) { return std::u32string(StripView(s)); }

    // Length in codepoints (view-based)
    [[nodiscard]]
    inline std::size_t Len(const std::u32string_view s) noexcept { return s.size(); }

    // Contains any char from set (view-based)
    [[nodiscard]]
    inline bool AnyOf(const std::u32string_view s, const std::u32string_view set) noexcept {
        return std::any_of(s.begin(), s.end(),
                           [&](const char32_t ch) noexcept { return Contains(set, ch); });
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
    //
    // Returns a view into `token` (either the whole token, or token[0 to unit_len]).
    [[nodiscard]]
    inline std::u32string_view CollapseRepeatedToken(const std::u32string_view &token) noexcept {
        const std::size_t length = token.size();
        if (length < 4 || length > 200)
            return token;

        for (std::size_t unit_len = 4; unit_len <= 10 && unit_len <= length / 3; ++unit_len) {
            if (length % unit_len != 0)
                continue;

            // Compare all chunks against the first unit, without allocating `unit`.
            bool all_match = true;
            for (std::size_t pos = unit_len; pos < length; pos += unit_len) {
                if (token.compare(pos, unit_len, token.substr(0, unit_len)) != 0) {
                    all_match = false;
                    break;
                }
            }

            if (all_match) {
                return token.substr(0, unit_len);
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
    inline std::vector<std::u32string_view>
    CollapseRepeatedWordSequences(const std::vector<std::u32string_view> &parts) {
        constexpr int minRepeats = 3;
        constexpr int maxPhraseLen = 8;

        const std::size_t n = parts.size();
        if (n < static_cast<std::size_t>(minRepeats))
            return parts;

        for (std::size_t start = 0; start < n; ++start) {
            for (int phraseLen = 1;
                 phraseLen <= maxPhraseLen && start + static_cast<std::size_t>(phraseLen) <= n;
                 ++phraseLen) {
                std::size_t count = 1;
                while (true) {
                    const std::size_t nextStart = start + count * static_cast<std::size_t>(phraseLen);
                    if (nextStart + static_cast<std::size_t>(phraseLen) > n)
                        break;

                    bool equal = true;
                    for (int k = 0; k < phraseLen; ++k) {
                        if (parts[start + static_cast<std::size_t>(k)] !=
                            parts[nextStart + static_cast<std::size_t>(k)]) {
                            equal = false;
                            break;
                        }
                    }
                    if (!equal)
                        break;

                    ++count;
                }

                if (count < static_cast<std::size_t>(minRepeats))
                    continue;

                std::vector<std::u32string_view> result;
                result.reserve(n - (count - 1) * static_cast<std::size_t>(phraseLen));

                for (std::size_t i = 0; i < start; ++i)
                    result.push_back(parts[i]);

                for (int k = 0; k < phraseLen; ++k)
                    result.push_back(parts[start + static_cast<std::size_t>(k)]);

                const std::size_t tailStart = start + count * static_cast<std::size_t>(phraseLen);
                for (std::size_t i = tailStart; i < n; ++i)
                    result.push_back(parts[i]);

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

        // Split into token views (no allocation per token).
        std::vector<std::u32string_view> parts;
        parts.reserve(16);

        const char32_t *data = line.data();
        const std::size_t n = line.size();

        std::size_t i = 0;
        while (i < n) {
            while (i < n && (data[i] == U' ' || data[i] == U'\t'))
                ++i;
            if (i >= n)
                break;

            const std::size_t start = i;
            while (i < n && data[i] != U' ' && data[i] != U'\t')
                ++i;

            parts.emplace_back(data + start, i - start);
        }

        if (parts.empty())
            return line;

        // 1) Phrase-level collapse (views)
        parts = CollapseRepeatedWordSequences(parts);

        // 2) Token-level collapse (views) + join
        std::u32string out;
        out.reserve(line.size()); // safe upper bound

        bool first = true;
        for (const auto tok: parts) {
            const std::u32string_view collapsed = CollapseRepeatedToken(tok);
            if (!first)
                out.push_back(U' ');
            out.append(collapsed.begin(), collapsed.end());
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
    // Detect lines like:
    //   書名：假面遊戲
    //   作者 : 東野圭吾
    //   出版時間　2024-03-12
    //   ISBN 9787573506078
    inline bool IsMetadataLine(const std::u32string_view line) noexcept {
        const std::u32string_view s = StripView(line);
        if (s.empty())
            return false;

        if (s.size() > 30)
            return false;

        // Find first separator
        std::size_t sep_idx = std::u32string_view::npos;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (const char32_t ch = s[i]; !Contains(METADATA_SEPARATORS, ch))
                continue;

            // Separator too early or too far
            if (i == 0 || i > 10)
                return false;

            sep_idx = i;
            break;
        }

        if (sep_idx == std::u32string_view::npos)
            return false;

        // Key
        const std::u32string_view key = StripView(s.substr(0, sep_idx));
        if (key.empty())
            return false;

        if (!IsMetadataKey(key))
            return false;

        // Skip spaces after separator
        std::size_t j = sep_idx + 1;
        while (j < s.size()) {
            if (const char32_t c = s[j]; c == U' ' || c == U'\t' || c == U'\r' || c == U'\u3000')
                ++j;
            else
                break;
        }

        if (j >= s.size())
            return false;

        // Dialog-like content guard
        if (Contains(DIALOG_OPENERS, s[j]))
            return false;

        return true;
    }

    // ------------------------- Title & heading heuristics -------------------------
    //
    // Title / chapter heading detection (regex-simulated, allocation-free).
    //
    // Matches short, standalone title-like lines, equivalent to the following C# regex:
    //
    //   ^(?!.*[,，])(?=.{0,50}$)
    //   (
    //     前言 | 序章 | 楔子 | 终章 | 尾声 | 后记 | 尾聲 | 後記 |
    //     番外.{0,15} |
    //     .{0,10}?第.{0,5}?([章节部卷節回][^分合的]) |
    //     (?:卷|章)[一二三四五六七八九十](?:$|.{0,20}?)
    //   )
    //
    // Semantics:
    //  - Line length ≤ 50 characters
    //  - Must NOT contain ASCII or full-width commas (',' / '，')
    //  - Fixed title words match only at the start of the line
    //  - “番外” allows a short suffix (≤ 15 chars)
    //  - Chapter-like patterns:
    //      * “第…章/节/部/卷/回”, excluding 分 / 合 / 的 immediately after the marker
    //      * “卷X” / “章X” where X is a Chinese numeral (一 to 十), with optional short tail
    //
    // Implemented as step-by-step scans for clarity, debuggability, and
    // cross-language consistency (C# / Java / Rust / Python).
    //
    inline bool IsTitleHeading(const std::u32string_view s_left) noexcept {
        const std::size_t len = s_left.size();
        if (len == 0 || len > 50)
            return false;

        // (?!.*[,，])  → reject if contains comma anywhere
        if (std::find(s_left.begin(), s_left.end(), U',') != s_left.end() ||
            std::find(s_left.begin(), s_left.end(), U'，') != s_left.end()) {
            return false;
        }

        // 1) Fixed title words + 番外.{0,15}
        for (const std::u32string_view w: TITLE_WORDS) {
            if (s_left.rfind(w, 0) != 0)
                continue;

            // 番外.{0,15}
            if (w == U"番外") {
                // 2 chars + up to 15 chars suffix
                return len <= (2 + 15);
            }

            return true;
        }

        // 2) .{0,10}?第.{0,5}?([章节部卷節回][^分合的])
        {
            // Find '第' within first 10 chars
            std::size_t di = std::u32string_view::npos;
            const std::size_t max_before_di = std::min<std::size_t>(10, len - 1);
            for (std::size_t i = 0; i <= max_before_di; ++i) {
                if (s_left[i] == U'第') {
                    di = i;
                    break;
                }
            }

            if (di != std::u32string_view::npos) {
                // After '第', scan up to 5 chars to find a chapter marker
                const std::size_t max_marker_pos = std::min<std::size_t>(len - 1, di + 1 + 5);
                for (std::size_t j = di + 1; j <= max_marker_pos; ++j) {
                    if (const char32_t ch = s_left[j]; !Contains(CHAPTER_MARKERS, ch))
                        continue;

                    // Next char must NOT be 分 / 合 / 的
                    if (j + 1 < len) {
                        if (Contains(EXCLUDED_CHAPTER_MARKERS_PREFIX, s_left[j + 1]))
                            continue;
                    }

                    // Regex has no further tail constraint here (only overall len<=50)
                    return true;
                }
            }
        }

        // 3) (?:卷|章)[一二三四五六七八九十](?:$|.{0,20}?)
        // Anchor is '^' in regex, so this must match from start.
        if (len >= 2 && (s_left[0] == U'卷' || s_left[0] == U'章') &&
            Contains(CN_NUM_1_TO_10, s_left[1])) {
            // (?:$|.{0,20}?)  → either ends right there, or allow up to 20 more chars
            return (len == 2) || (len - 2 <= 20);
        }

        return false;
    }

    inline bool IsHeadingLike(const std::u32string_view raw) noexcept {
        const std::u32string_view s = StripView(raw);
        if (s.empty()) return false;

        // Keep page markers intact
        if (IsPageMarker(s)) return false;

        // Reject headings with unclosed brackets
        if (HasUnclosedBracket(s))
            return false;

        // Get last meaningful character (robust against whitespace changes)
        std::size_t lastIdx{};
        char32_t last{};

        if (!TryGetLastNonWhitespace(s, lastIdx, last))
            return false;

        // Determine dynamic max length:
        //   - CJK/mixed: SHORT_HEADING_MAX_LEN
        //   - pure ASCII: double
        const bool all_ascii = IsAllAscii(s);
        const std::size_t max_len =
                all_ascii || IsMixedCjkAscii(s) ? (SHORT_HEADING_MAX_LEN * 2) : SHORT_HEADING_MAX_LEN;
        const std::size_t len = s.size();

        // Short circuit for item title-like: "物品准备："
        if (IsColonLike(last) &&
            len <= max_len &&
            lastIdx > 0 && // need at least one char before ':'
            text::IsAllCjkNoWhitespace(s.substr(0, lastIdx))) {
            return true;
        }

        // Allow postfix closer with condition
        if (IsAllowedPostfixCloser(last) &&
            !ContainsAnyCommaLike(s.substr(0, lastIdx))) // only scan the meaningful prefix
        {
            return true;
        }

        // ✅ bracket-wrapped heading shortcut
        if (IsWrappedByMatchingBracket(s)) {
            if (const std::u32string inner = Strip(s.substr(1, s.size() - 2));
                !inner.empty() && IsMostlyCjk(inner)
            )
                return true;
        }

        // Reject any other clause or End Punct
        // Reject any short line containing comma-like separators
        if (IsClauseOrEndPunct(last) ||
            ContainsAnyCommaLike(s) ||
            ContainsStrongSentenceEnd(s)) {
            return false;
        }

        if (len > max_len) {
            return false;
        }

        // Analyze characters
        bool hasNonAscii = false;
        bool allAscii = true;
        bool hasLetter = false;
        bool allAsciiDigits = true;

        for (const char32_t ch: s) {
            if (ch > 0x7F) {
                hasNonAscii = true;
                allAscii = false;
                allAsciiDigits = false;
                continue;
            }

            // ASCII digit check
            if (!(ch >= U'0' && ch <= U'9')) {
                allAsciiDigits = false;
            }

            // ASCII letter check
            if ((ch >= U'a' && ch <= U'z') || (ch >= U'A' && ch <= U'Z')) {
                hasLetter = true;
            }
        }

        // ----------------- RULE SET -----------------

        // Rule C: pure ASCII digits → heading-like (e.g., "1", "02", "12")
        if (allAsciiDigits) {
            return true;
        }

        // Rule A: CJK / mixed short line → heading-like
        // (but not ending with comma-like punctuation)
        if (hasNonAscii && !IsCommaLike(last)) {
            return true;
        }

        // Rule B: pure ASCII short line with letters → heading-like
        if (allAscii && hasLetter) {
            return true;
        }

        return false;
    }

    // Indentation: "^\s{2,}" - we approximate: at least 2 leading spaces/full-width spaces
    inline bool IsIndented(const std::u32string_view raw_line) noexcept {
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
    inline bool IsChapterEnding(const std::u32string_view s) noexcept {
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

    inline bool IsVisualDividerLine(const std::u32string_view s) noexcept {
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

    // ------ Sentence Boundary start ------ //

    [[nodiscard]]
    inline bool IsAtEndAllowingClosers(const std::u32string_view s, const std::size_t index) noexcept {
        for (std::size_t j = index + 1; j < s.size(); ++j) {
            const char32_t ch = s[j];

            if (IsWhitespace(ch))
                continue;

            if (IsQuoteCloser(ch) || IsBracketCloser(ch))
                continue;

            return false;
        }
        return true;
    }

    // Strict: the ASCII punct itself is the last non-whitespace char (level 3 strict rules).
    [[nodiscard]]
    inline bool IsOcrCjkAsciiPunctAtLineEnd(const std::u32string_view s, const std::size_t lastNonWsIndex) noexcept {
        if (lastNonWsIndex == 0)
            return false;

        // previous char exists (maybe whitespace-free already because lastNonWsIndex is meaningful)
        return IsCjk(s[lastNonWsIndex - 1]) && IsMostlyCjk(s);
    }

    // Relaxed "end": after index, only whitespace and closers are allowed.
    // Needed for patterns like: CJK '.' then closing quote/bracket: “.”」  .）
    [[nodiscard]]
    inline bool IsOcrCjkAsciiPunctBeforeClosers(const std::u32string_view s, const std::size_t index) noexcept {
        if (!IsAtEndAllowingClosers(s, index))
            return false;

        // Must have a previous *non-whitespace* character
        char32_t prev{};
        if (!TryGetPrevNonWhitespace(s, index, prev))
            return false;

        // Previous meaningful char must be CJK, and the line mostly CJK
        return IsCjk(prev) && IsMostlyCjk(s);
    }

    [[nodiscard]]
    inline bool EndsWithSentenceBoundary(const std::u32string_view s, const int level = 2) noexcept {
        if (s.empty())
            return false;

        // last non-whitespace
        std::size_t lastIdx{};
        char32_t last{};
        if (!TryGetLastNonWhitespace(s, lastIdx, last))
            return false;

        // ---- STRICT rules (level >= 3) ----
        // 1) Strong sentence end
        if (IsStrongSentenceEnd(last))
            return true;

        if (level >= 3) {
            if ((last == U'.' || last == U':') && IsOcrCjkAsciiPunctAtLineEnd(s, lastIdx))
                return true;
        }

        // prev non-whitespace (before last-Non-Ws)
        std::size_t prevIdx{};
        char32_t prev{};

        // 2) Quote closers + Allowed postfix closer after strong end
        if (const bool hasPrev = TryGetPrevNonWhitespace(s, lastIdx, prevIdx, prev);
            (IsQuoteCloser(last) || IsAllowedPostfixCloser(last)) && hasPrev) {
            // Strong end immediately before quote closer
            if (IsStrongSentenceEnd(prev))
                return true;

            // OCR artifact: “.” where '.' acts like '。' (CJK context)
            // '.' is not the lastNonWs (quote is), so use the "before closers" version.
            if (prev == U'.' && IsOcrCjkAsciiPunctBeforeClosers(s, prevIdx))
                return true;
        }

        if (level >= 3)
            return false;

        // ---- LENIENT rules (level == 2) ----

        // 4) NEW: long Mostly-CJK line ending with full-width colon "："
        // Treat as a weak boundary (common in novels: "他说：" then dialog starts next line)
        if (last == U'：' && IsMostlyCjk(s))
            return true;

        // Level 2 (lenient): allow ellipsis as weak boundary
        if (EndsWithEllipsis(s))
            return true;

        if (level >= 2)
            return false;

        // ---- VERY LENIENT rules (level == 1) ----
        return last == U'；' || last == U'：' || last == U';' || last == U':';
    }

    // ------ Sentence Boundary end ------ //

    // ------ Bracket Boundary start ------

    [[nodiscard]]
    inline bool EndsWithCjkBracketBoundary(std::u32string_view s) noexcept {
        // Equivalent to string.IsNullOrWhiteSpace
        s = text::TrimView(s);
        if (s.empty())
            return false;

        if (s.size() < 2)
            return false;

        const char32_t open = s.front();

        // 1) Must be one of our known pairs.
        if (const char32_t close = s.back(); !IsMatchingBracket(open, close))
            return false;

        // Inner content (exclude the outer bracket pair), trimmed
        const auto inner = text::TrimView(s.substr(1, s.size() - 2));
        if (inner.empty())
            return false;

        // 2) Must be mostly CJK (reject "(test)", "[1.2]", etc.)
        if (!IsMostlyCjk(inner))
            return false;

        // ASCII bracket pairs are suspicious → require at least one CJK inside
        if ((open == U'(' || open == U'[') && !ContainsAnyCjk(inner))
            return false;

        // 3) Ensure this bracket type is balanced inside the text
        //    (prevents malformed OCR / premature close)
        return IsBracketTypeBalanced(s, open);
    }

    // ------ Bracket Boundary end ------

    // Close namespaces opened above
} // namespace pdfium::detail
