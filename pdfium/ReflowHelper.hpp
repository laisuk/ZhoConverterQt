#pragma once
//
// ReflowHelper.hpp
// -----------------------------------------------------------------------------
// Standalone CJK paragraph reflow helpers (UTF-8 in / UTF-8 out).
//
// This header contains the text shaping / paragraph reflow logic that was
// originally embedded in PdfiumHelper.hpp. It is intentionally PDF-backend
// agnostic so it can be reused by other extractors (PDFBox, Poppler, etc.).
//
// Public API:
//   std::string pdfium::ReflowCjkParagraphs(const std::string& utf8Text,
//                                          bool addPdfPageHeader,
//                                          bool compact);
//
// Notes:
// - The implementation is header-only (inline) for ease of integration.
// - Behavior is kept identical to the original implementation it was extracted
//   from (refactor is organizational + formatting only).
// -----------------------------------------------------------------------------

#include "ReflowCommon.hpp"

namespace pdfium {
    namespace detail {

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
                        if (char32_t last_char = buffer.back(); !Contains(CLAUSE_OR_END_PUNCT.data(), last_char)) {
                            // treat as layout gap, skip
                            continue;
                        }
                    }

                    flush_buffer();
                    continue;
                }

                // 2) Visual divider / box drawing line (MUST force paragraph break)
                if (IsVisualDividerLine(stripped_left)) {
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
                                                                                DIALOG_OPENERS.data(), after_indent[0])) {
                            buffer += stripped;
                            dialog_state.update(stripped);
                            continue;
                        }
                    }
                }

                // 5) Ends with CJK punctuation → new paragraph if not inside unclosed dialog
                if (!buffer_text.empty() &&
                    Contains(CLAUSE_OR_END_PUNCT.data(), buffer_text.back()) &&
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
