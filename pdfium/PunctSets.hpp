#pragma once
#include <string_view>

namespace pdfium::text::punct {
    // Tier 2: clause-or-end-ish (looser heuristics, not always a true sentence end)
    inline constexpr std::u32string_view CLAUSE_OR_END_PUNCT =
            U"。！？；：…—”」’』）】》〗〕〉］｝＞.!?):?>";

    [[nodiscard]]
    [[gnu::always_inline]] inline bool IsClauseOrEndPunct(char32_t ch) noexcept {
        return CLAUSE_OR_END_PUNCT.find(ch) != std::u32string_view::npos;
    }

    // Dialog brackets (from DIALOG_OPEN_TO_CLOSE)
    inline constexpr std::u32string_view DIALOG_OPENERS = U"“‘「『";
    inline constexpr std::u32string_view DIALOG_CLOSERS = U"”’」』";

    // Tier 1: hard sentence enders (safe for "flush now")
    // (Matches your C#: '。' '！' '？' '!' '?')
    [[nodiscard]]
    [[gnu::always_inline]] inline bool IsStrongSentenceEnd(char32_t ch) noexcept {
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

    [[nodiscard]] inline bool is_bracket_opener(char32_t ch) noexcept {
        return std::any_of(std::begin(BRACKET_PAIRS), std::end(BRACKET_PAIRS),
                           [ch](const auto &p) { return p.first == ch; });
    }

    [[nodiscard]] inline bool is_bracket_closer(char32_t ch) noexcept {
        return std::any_of(std::begin(BRACKET_PAIRS), std::end(BRACKET_PAIRS),
                           [ch](const auto &p) { return p.second == ch; });
    }

    [[nodiscard]] inline bool is_matching_bracket(char32_t open, char32_t close) noexcept {
        return std::any_of(std::begin(BRACKET_PAIRS), std::end(BRACKET_PAIRS),
                           [open, close](const auto &p) { return p.first == open && p.second == close; });
    }
} // namespace pdfium::text::punct
