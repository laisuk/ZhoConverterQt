#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pdfium::text {
    // ---------- Unicode whitespace (deterministic; avoids locale-dependent iswspace) ----------

    [[nodiscard]]
    [[gnu::always_inline]] inline bool IsWhitespace(const char32_t ch) noexcept {
        // ASCII whitespace
        if (ch == U' ' || ch == U'\t' || ch == U'\n' || ch == U'\r' || ch == U'\f' || ch == U'\v')
            return true;

        // Common Unicode whitespace
        switch (ch) {
            case 0x00A0: // NO-BREAK SPACE
            case 0x1680: // OGHAM SPACE MARK
            case 0x2000: // EN QUAD
            case 0x2001: // EM QUAD
            case 0x2002: // EN SPACE
            case 0x2003: // EM SPACE
            case 0x2004: // THREE-PER-EM SPACE
            case 0x2005: // FOUR-PER-EM SPACE
            case 0x2006: // SIX-PER-EM SPACE
            case 0x2007: // FIGURE SPACE
            case 0x2008: // PUNCTUATION SPACE
            case 0x2009: // THIN SPACE
            case 0x200A: // HAIR SPACE
            case 0x2028: // LINE SEPARATOR
            case 0x2029: // PARAGRAPH SEPARATOR
            case 0x202F: // NARROW NO-BREAK SPACE
            case 0x205F: // MEDIUM MATHEMATICAL SPACE
            case 0x3000: // IDEOGRAPHIC SPACE (CJK)
                return true;
            default:
                return false;
        }
    }

    // ---------- TryGet helpers ----------

    /// Try to get the last non-whitespace character from a view.
    /// Returns index (0 to size-1) and the character.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool TryGetLastNonWhitespace(
        const std::u32string_view s,
        std::size_t &lastIdx,
        char32_t &last) noexcept {
        lastIdx = static_cast<std::size_t>(-1);
        last = U'\0';

        if (s.empty())
            return false;

        for (std::size_t i = s.size(); i-- > 0;) {
            const char32_t ch = s[i];
            if (IsWhitespace(ch))
                continue;

            lastIdx = i;
            last = ch;
            return true;
        }
        return false;
    }

    /// Convenience overload when you only need the character.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool TryGetLastNonWhitespace(
        const std::u32string_view s,
        char32_t &last) noexcept {
        std::size_t idx{};
        return TryGetLastNonWhitespace(s, idx, last);
    }

    /// Try to get last and previous non-whitespace characters.
    /// Semantics match C#:
    /// - returns false only if "last" doesn't exist
    /// - returns true if "last" exists even when "prev" doesn't (prevIdx = npos)
    [[nodiscard]]
    [[gnu::always_inline]] inline bool
    TryGetLastTwoNonWhitespace(const std::u32string_view s,
                               std::size_t &lastIdx, char32_t &last,
                               std::size_t &prevIdx, char32_t &prev) noexcept {
        lastIdx = prevIdx = static_cast<std::size_t>(-1);
        last = prev = U'\0';

        if (!TryGetLastNonWhitespace(s, lastIdx, last))
            return false;

        // Find previous non-ws before lastIdx
        if (lastIdx == 0) {
            // found last, but no prev
            prevIdx = static_cast<std::size_t>(-1);
            prev = U'\0';
            return true;
        }

        for (std::size_t i = lastIdx; i-- > 0;) {
            const char32_t ch = s[i];
            if (IsWhitespace(ch))
                continue;

            prevIdx = i;
            prev = ch;
            return true;
        }

        // found last, but no prev
        prevIdx = static_cast<std::size_t>(-1);
        prev = U'\0';
        return true;
    }

    /// Try to get the previous non-whitespace char before beforeIndex in a view.
    /// Returns false if not found.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool
    TryGetPrevNonWhitespace(const std::u32string_view s,
                            const std::size_t beforeIndex,
                            std::size_t &prevIndex,
                            char32_t &prevChar) noexcept {
        prevIndex = static_cast<std::size_t>(-1);
        prevChar = U'\0';

        if (s.empty() || beforeIndex == 0)
            return false;

        // Start from min(beforeIndex-1, s.size()-1)
        std::size_t i = beforeIndex - 1;
        if (i >= s.size())
            i = s.size() - 1;

        for (std::size_t k = i + 1; k-- > 0;) {
            const char32_t ch = s[k];
            if (IsWhitespace(ch))
                continue;

            prevIndex = k;
            prevChar = ch;
            return true;
        }

        return false;
    }

    /// Try to get the previous non-whitespace char before beforeIndex (char-only).
    [[nodiscard]]
    [[gnu::always_inline]] inline bool
    TryGetPrevNonWhitespace(const std::u32string_view s,
                            const std::size_t beforeIndex,
                            char32_t &prevChar) noexcept {
        std::size_t idx{};
        return TryGetPrevNonWhitespace(s, beforeIndex, idx, prevChar);
    }

    [[nodiscard]]
    [[gnu::always_inline]] inline bool
    TryGetFirstNonWhitespace(const std::u32string_view s,
                             std::size_t &firstIdx,
                             char32_t &first) noexcept {
        firstIdx = static_cast<std::size_t>(-1);
        first = U'\0';

        for (std::size_t i = 0; i < s.size(); ++i) {
            const char32_t ch = s[i];
            if (IsWhitespace(ch))
                continue;
            firstIdx = i;
            first = ch;
            return true;
        }
        return false;
    }

    [[nodiscard]]
    [[gnu::always_inline]] inline std::u32string_view
    TrimView(const std::u32string_view s) noexcept {
        std::size_t firstIdx{};
        if (char32_t first{}; !TryGetFirstNonWhitespace(s, firstIdx, first))
            return {}; // all whitespace

        std::size_t lastIdx{};
        char32_t last{};
        (void) TryGetLastNonWhitespace(s, lastIdx, last); // must succeed if first succeeded

        return s.substr(firstIdx, lastIdx - firstIdx + 1);
    }

    // ---------- CJK / ASCII classifiers (IsCjkXXX) ----------

    [[nodiscard]] inline bool IsCjk(const char32_t ch) noexcept {
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

    // Contains any CJK (very simple heuristic: >0x7F)
    inline bool ContainsCjk(const std::u32string_view s) noexcept {
        return std::any_of(s.begin(), s.end(),
                           [](const char32_t ch) { return ch > 0x7F; });
    }

    inline bool IsAscii(const char32_t ch) {
        return ch <= 0x7F;
    }

    // All ASCII?
    inline bool IsAllAscii(const std::u32string_view s) noexcept {
        return std::all_of(s.begin(), s.end(), IsAscii);
    }

    // Any A-Z / a-z
    inline bool HasLatinAlpha(const std::u32string_view s) noexcept {
        return std::any_of(s.begin(), s.end(), [](const char32_t ch) {
            return (ch >= U'a' && ch <= U'z') || (ch >= U'A' && ch <= U'Z');
        });
    }

    [[nodiscard]] inline bool IsAsciiDigit(const char32_t ch) noexcept {
        return ch >= U'0' && ch <= U'9';
    }

    [[nodiscard]] inline bool IsAsciiLetter(const char32_t ch) noexcept {
        return (ch >= U'A' && ch <= U'Z') || (ch >= U'a' && ch <= U'z');
    }

    [[nodiscard]] inline bool IsAsciiLetterOrDigit(const char32_t ch) noexcept {
        return (ch >= U'0' && ch <= U'9') ||
               (ch >= U'a' && ch <= U'z') ||
               (ch >= U'A' && ch <= U'Z');
    }

    // Full-width digits: '０'..'９'
    [[nodiscard]] inline bool IsFullwidthDigit(const char32_t ch) noexcept {
        return ch >= U'０' && ch <= U'９';
    }

    // Neutral ASCII allowed in "mixed CJK + ASCII" lines: space - / : .
    [[nodiscard]] inline bool IsNeutralAsciiForMixed(const char32_t ch) noexcept {
        return ch == U' ' || ch == U'-' || ch == U'/' || ch == U':' || ch == U'.';
    }

    // Mixed CJK + ASCII (like "第3章 Chapter 1", "iPhone 16 Pro Max", etc.)
    // Rules (matching your C# intent):
    // - Allow neutral ASCII separators: space - / : .
    // - ASCII content must be letter/digit (other ASCII punctuation rejects)
    // - Allow full-width digits
    // - Non-ASCII must be CJK
    // - Return true only if both CJK and ASCII content appear.
    [[nodiscard]] inline bool IsMixedCjkAscii(const std::u32string_view s) noexcept {
        bool hasCjk = false;
        bool hasAscii = false;

        for (const char32_t ch: s) {
            if (IsNeutralAsciiForMixed(ch))
                continue;

            if (ch <= 0x7F) {
                if (IsAsciiLetterOrDigit(ch)) {
                    hasAscii = true;
                } else {
                    return false; // reject other ASCII punct
                }
                continue;
            }

            if (IsFullwidthDigit(ch)) {
                hasAscii = true;
                continue;
            }

            if (IsCjk(ch)) {
                hasCjk = true;
                continue;
            }

            // Other non-ASCII characters disqualify
            return false;
        }

        return hasCjk && hasAscii;
    }

    [[nodiscard]]
    inline bool IsMostlyCjk(const std::u32string_view s) noexcept {
        std::size_t cjk = 0;
        std::size_t ascii = 0;

        for (const char32_t ch: s) {
            // Neutral whitespace
            if (IsWhitespace(ch)) {
                continue;
            }

            // Neutral digits (ASCII + FULLWIDTH)
            if (IsAsciiDigit(ch) || IsFullwidthDigit(ch)) {
                continue;
            }

            // CJK Unified Ideographs
            if (IsCjk(ch)) {
                ++cjk;
                continue;
            }

            // Count ASCII letters only; ASCII punctuation is neutral
            if (ch <= 0x7F && IsAsciiLetter(ch)) {
                ++ascii;
            }
        }

        return cjk > 0 && cjk >= ascii;
    }

    /// Returns true if the span consists entirely of CJK characters.
    /// Whitespace handling is controlled by allowWhitespace.
    /// Returns false for empty or whitespace-only spans.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool
    IsAllCjk(const std::u32string_view s, const bool allowWhitespace = false) noexcept {
        bool seen = false;

        for (const char32_t ch: s) {
            if (IsWhitespace(ch)) {
                if (!allowWhitespace)
                    return false;
                continue;
            }

            seen = true;

            if (!IsCjk(ch))
                return false;
        }

        return seen;
    }

    /// Returns true if the span consists entirely of CJK characters,
    /// ignoring whitespace. Returns false for empty or whitespace-only spans.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool
    IsAllCjkIgnoringWhitespace(const std::u32string_view s) noexcept {
        return IsAllCjk(s, /*allowWhitespace=*/true);
    }

    /// Returns true if the span consists entirely of CJK characters,
    /// and contains no whitespace. Returns false for empty spans.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool
    IsAllCjkNoWhitespace(const std::u32string_view s) noexcept {
        return IsAllCjk(s, /*allowWhitespace=*/false);
    }

    /// Returns true if the span contains at least one CJK character.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool
    ContainsAnyCjk(const std::u32string_view s) noexcept {
        return std::any_of(s.begin(), s.end(),
                           [](const char32_t ch) noexcept {
                               return IsCjk(ch);
                           });
    }

    // =========================
    //  Ellipsis
    // =========================

    [[nodiscard]]
    [[gnu::always_inline]] inline bool
    EndsWithEllipsis(const std::u32string_view s) noexcept {
        if (s.empty())
            return false;

        // Strong CJK gate: ellipsis only meaningful in CJK context
        if (!IsMostlyCjk(s))
            return false;

        // Walk back to last non-whitespace
        std::size_t i = s.size();
        while (i > 0 && text::IsWhitespace(s[i - 1]))
            --i;

        if (i == 0)
            return false;

        // Single Unicode ellipsis
        if (s[i - 1] == U'…')
            return true;

        // OCR case: ASCII "..."
        return i >= 3 &&
               s[i - 1] == U'.' &&
               s[i - 2] == U'.' &&
               s[i - 3] == U'.';
    }
} // namespace pdfium::text
