#pragma once
#include <string_view>

namespace pdfium::text::punct {
    // Tier 2: clause-or-end-ish (looser heuristics, not always a true sentence end)
    inline constexpr std::u32string_view CLAUSE_OR_END_PUNCT =
            U"。！？；：…—”」’』）】》〗〕〉］｝＞.!?):?>";

    [[nodiscard]]
    [[gnu::always_inline]] inline bool IsClauseOrEndPunct(const char32_t ch) noexcept {
        return CLAUSE_OR_END_PUNCT.find(ch) != std::u32string_view::npos;
    }

    // Dialog brackets (from DIALOG_OPEN_TO_CLOSE)
    inline constexpr std::u32string_view DIALOG_OPENERS = U"“‘「『";
    inline constexpr std::u32string_view DIALOG_CLOSERS = U"”’」』";

    /// Returns true if the character is a dialog opening quote/bracket.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool IsDialogOpener(const char32_t ch) noexcept {
        return DIALOG_OPENERS.find(ch) != std::u32string_view::npos;
    }

    /// Returns true if the character is a dialog closing quote/bracket.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool IsDialogCloser(const char32_t ch) noexcept {
        return DIALOG_CLOSERS.find(ch) != std::u32string_view::npos;
    }

    /// Quote closer (alias of dialog closer).
    [[nodiscard]]
    [[gnu::always_inline]] inline bool IsQuoteCloser(const char32_t ch) noexcept {
        return IsDialogCloser(ch);
    }

    /// Returns true if the character is either a dialog opener or closer.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool IsDialogBracket(const char32_t ch) noexcept {
        return IsDialogOpener(ch) || IsDialogCloser(ch);
    }

    /// Returns true if the line starts with a dialog opener
    /// after skipping leading whitespace and indentation.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool BeginsWithDialogOpener(const std::u32string_view s) noexcept {
        for (const char32_t ch: s) {
            if (IsWhitespace(ch))
                continue;

            return IsDialogOpener(ch);
        }
        return false;
    }

    // Tier 1: hard sentence enders (safe for "flush now")
    // (Matches your C#: '。' '！' '？' '!' '?')
    [[nodiscard]]
    [[gnu::always_inline]] inline bool IsStrongSentenceEnd(const char32_t ch) noexcept {
        switch (ch) {
            case U'。':
            case U'！':
            case U'？':
            case U'!':
            case U'?':
                return true;
            default:
                return false;
        }
    }

    /// Returns true if the string contains any Tier-1 strong sentence ender
    /// (e.g. 。！？!?).
    [[nodiscard]]
    [[gnu::always_inline]] inline bool ContainsStrongSentenceEnd(const std::u32string_view s) noexcept {
        return std::any_of(s.begin(), s.end(),
                           [](const char32_t ch) noexcept {
                               return IsStrongSentenceEnd(ch);
                           });
    }

    /// Returns true if the string ends with a Tier-1 strong sentence ender
    /// after trimming trailing whitespace.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool EndsWithStrongSentenceEnd(const std::u32string_view s) noexcept {
        char32_t ch{};
        return TryGetLastNonWhitespace(s, ch) &&
               IsStrongSentenceEnd(ch);
    }

    // -------------------------
    // Soft continuation punctuation
    // -------------------------

    // Comma-like separators (soft continuation, not sentence end)
    inline constexpr std::u32string_view COMMA_LIKE_CHARS = U"，,、";

    /// Returns true if the character is a comma-like separator.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool IsCommaLike(const char32_t ch) noexcept {
        return COMMA_LIKE_CHARS.find(ch) != std::u32string_view::npos;
    }

    /// Returns true if the string contains any comma-like
    /// separator characters (e.g. ， , 、).
    [[nodiscard]]
    [[gnu::always_inline]] inline bool ContainsAnyCommaLike(const std::u32string_view s) noexcept {
        return std::any_of(s.begin(), s.end(),
                           [](const char32_t ch) noexcept {
                               return IsCommaLike(ch);
                           });
    }

    // -------------------------
    // Colon-like punctuation
    // -------------------------

    /// Returns true if the character is a colon-like separator.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool IsColonLike(const char32_t ch) noexcept {
        return ch == U'：' || ch == U':';
    }

    /// Returns true if the string ends with a colon-like character
    /// after trimming trailing whitespace.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool EndsWithColonLike(const std::u32string_view s) noexcept {
        char32_t last{};
        return TryGetLastNonWhitespace(s, last) &&
               IsColonLike(last);
    }

    /// Returns true if the character is an allowed postfix closer (used in heading/metadata rules).
    [[nodiscard]]
    [[gnu::always_inline]] inline bool IsAllowedPostfixCloser(const char32_t ch) noexcept {
        return ch == U'）' || ch == U')';
    }

    /// Returns true if the string ends with an allowed postfix closer
    /// after trimming trailing whitespace.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool EndsWithAllowedPostfixCloser(const std::u32string_view s) noexcept {
        char32_t last{};
        return TryGetLastNonWhitespace(s, last) &&
               IsAllowedPostfixCloser(last);
    }

    // -----------------------------------------------------------------------------
    // Bracket punctuation table (open → close)
    // -----------------------------------------------------------------------------

    inline constexpr std::pair<char32_t, char32_t> BRACKET_PAIRS[] = {
        // Parentheses
        {U'（', U'）'},
        {U'(', U')'},

        // Square brackets
        {U'［', U'］'},
        {U'[', U']'},

        // Curly braces
        {U'｛', U'｝'},
        {U'{', U'}'},

        // Angle brackets
        {U'＜', U'＞'},
        {U'<', U'>'},
        {U'⟨', U'⟩'},
        {U'〈', U'〉'},

        // CJK brackets
        {U'【', U'】'},
        {U'《', U'》'},
        {U'〔', U'〕'},
        {U'〖', U'〗'},
    };

    // -----------------------------------------------------------------------------
    // Bracket helpers
    // -----------------------------------------------------------------------------

    [[nodiscard]]
    [[gnu::always_inline]] inline bool IsBracketOpener(char32_t ch) noexcept {
        return std::any_of(std::begin(BRACKET_PAIRS), std::end(BRACKET_PAIRS),
                           [ch](const auto &p) { return p.first == ch; });
    }

    [[nodiscard]]
    [[gnu::always_inline]] inline bool IsBracketCloser(char32_t ch) noexcept {
        return std::any_of(std::begin(BRACKET_PAIRS), std::end(BRACKET_PAIRS),
                           [ch](const auto &p) { return p.second == ch; });
    }

    [[nodiscard]]
    [[gnu::always_inline]] inline bool IsMatchingBracket(char32_t open, char32_t close) noexcept {
        return std::any_of(std::begin(BRACKET_PAIRS), std::end(BRACKET_PAIRS),
                           [open, close](const auto &p) { return p.first == open && p.second == close; });
    }

    // minLen=3 means at least: open + 1 char + close
    [[nodiscard]]
    [[gnu::always_inline]] inline bool IsWrappedByMatchingBracket(const std::u32string_view s,
                                                                  const char32_t lastNonWs,
                                                                  const std::size_t minLen = 3) noexcept {
        return s.size() >= minLen && IsMatchingBracket(s.front(), lastNonWs);
    }

    [[nodiscard]]
    [[gnu::always_inline]] inline bool IsWrappedByMatchingBracket(const std::u32string_view s,
                                                                  const std::size_t minLen = 3) noexcept {
        char32_t last{};
        return TryGetLastNonWhitespace(s, last) &&
               IsWrappedByMatchingBracket(s, last, minLen);
    }

    /// Try to get the matching closer for an opening bracket.
    /// Returns false if the opener is not in the table.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool TryGetMatchingCloser(const char32_t open, char32_t &close) noexcept {
        for (const auto &[key, val]: BRACKET_PAIRS) {
            if (key == open) {
                close = val;
                return true;
            }
        }
        return false;
    }

    /// Returns true if all brackets of the given type are balanced in the span.
    /// Stray closers or negative depth are treated as unbalanced.
    [[nodiscard]]
    [[gnu::always_inline]] inline bool
    IsBracketTypeBalanced(const std::u32string_view s, const char32_t open) noexcept {
        char32_t close{};
        if (!TryGetMatchingCloser(open, close))
            return true; // unknown opener => treat as safe (matches C#)

        int depth = 0;
        for (const char32_t ch: s) {
            if (ch == open) {
                ++depth;
            } else if (ch == close) {
                --depth;
                if (depth < 0)
                    return false;
            }
        }

        return depth == 0;
    }

    // Assumed to exist (you likely already have these or will add them):
    //   - bool IsBracketOpener(char32_t);
    //   - bool IsBracketCloser(char32_t);
    //   - bool IsMatchingBracket(char32_t open, char32_t close);

    [[nodiscard]]
    inline bool HasUnclosedBracket(const std::u32string_view s) noexcept {
        if (s.empty())
            return false;

        std::array<char32_t, 16> small{};
        std::vector<char32_t> big; // allocated only if nesting > 16
        std::size_t top = 0;
        bool seenBracket = false;

        auto push = [&](const char32_t ch) {
            if (top < small.size()) {
                small[top++] = ch;
                return;
            }
            if (big.empty()) {
                big.reserve(32);
                big.assign(small.begin(), small.end());
            }
            big.push_back(ch);
            ++top;
        };

        auto pop = [&]() -> char32_t {
            // Precondition: top > 0
            if (top <= small.size()) {
                return small[--top];
            }
            // top > small.size() implies big is in use
            const char32_t ch = big.back();
            big.pop_back();
            --top;
            return ch;
        };

        for (const char32_t ch: s) {
            if (IsBracketOpener(ch)) {
                seenBracket = true;
                push(ch);
                continue;
            }

            if (!IsBracketCloser(ch))
                continue;

            seenBracket = true;

            // stray closer
            if (top == 0)
                return true;

            // mismatched closer
            if (const char32_t open = pop(); !IsMatchingBracket(open, ch))
                return true;
        }

        // unclosed opener(s)
        return seenBracket && top != 0;
    }
} // namespace pdfium::text::punct
