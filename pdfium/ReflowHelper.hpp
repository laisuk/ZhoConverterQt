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
            DialogState dialogState;

            auto flush_buffer = [&]() {
                if (!buffer.empty()) {
                    segments.push_back(buffer);
                    buffer.clear();
                    dialogState.reset();
                }
            };

            for (const auto &raw_line32: lines32) {
                std::u32string stripped = RStrip(raw_line32);
                std::u32string stripped_left = LStrip(stripped);

                // NEW: style-layer repeat collapse applied at paragraph level.
                stripped = CollapseRepeatedSegments(stripped);

                // IMPORTANT: collapse may change leading layout; recompute probe
                stripped_left = LStrip(stripped);

                const bool hasUnclosedBracket = HasUnclosedBracket(buffer);

                // 1) Empty line
                if (stripped.empty()) {
                    if (!addPdfPageHeader && !buffer.empty()) {
                        // NEW: If dialog is unclosed, or buffer has bracket issues, always treat blank line as soft.
                        // Never flush mid-dialog / mid-parenthesis due to cross-page artifacts.
                        if (dialogState.is_unclosed() || hasUnclosedBracket) {
                            continue;
                        }

                        // Light rule: only flush on blank line if buffer ends with STRONG sentence end.
                        // Otherwise, treat as a soft cross-page blank line and keep accumulating.
                        if (char32_t last{}; TryGetLastNonWhitespace(buffer, last) && !IsStrongSentenceEnd(last)) {
                            continue;
                        }
                    }

                    // End of paragraph → flush buffer (do NOT emit empty segments)
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

                // 3b) Weak heading-like (short heading):
                //     Only takes effect when the previous paragraph is "safe"
                //     AND the previous paragraph’s ending looks like a boundary.
                //     Otherwise, treat as continuation and fall through.
                if (is_short_heading) {
                    const bool isAllCjk = IsAllCjkIgnoringWhitespace(stripped);

                    bool splitAsHeading = false;

                    if (buffer.empty()) {
                        // Start of document / just flushed
                        splitAsHeading = true;
                    } else {
                        if (HasUnclosedBracket(buffer)) {
                            // Unsafe previous paragraph → must be continuation
                            splitAsHeading = false;
                        } else {
                            std::size_t prevIdx{};
                            char32_t last{}, prev{};

                            if (std::size_t lastIdx{};
                                !TryGetLastTwoNonWhitespace(buffer, lastIdx, last, prevIdx, prev)) {
                                // Buffer has no last non-ws at all (empty/whitespace-only) → treat like empty
                                splitAsHeading = true;
                            } else {
                                // NOTE: TryGetLastTwoNonWhitespace returns true even if prev doesn't exist.
                                // We only care about `last` here (same as C# logic).

                                const bool prevEndsWithCommaLike = IsCommaLike(last);
                                const bool prevEndsWithSentencePunct = IsClauseOrEndPunct(last);

                                const bool currentLooksLikeContinuationMarker =
                                        isAllCjk ||
                                        EndsWithColonLike(stripped) ||
                                        EndsWithAllowedPostfixCloser(stripped);

                                // Comma-ending → continuation
                                // All-CJK short heading-like + previous not ended → continuation
                                splitAsHeading =
                                        !prevEndsWithCommaLike &&
                                        !(currentLooksLikeContinuationMarker && !prevEndsWithSentencePunct);
                            }
                        }
                    }

                    if (splitAsHeading) {
                        // If we have a real previous paragraph, flush it first
                        flush_buffer();

                        // Current line becomes a standalone heading
                        segments.push_back(stripped);
                        // if stripped is view; otherwise just push_back(stripped)
                        continue;
                    }

                    // else: fall through → normal merge logic below
                }

                // ------ Current line finalizer ------

                // 7) Finalizer: strong sentence end → flush immediately. Do not remove.
                // If the current line completes a strong sentence, append it and flush immediately.
                if (!buffer.empty() &&
                    !dialogState.is_unclosed() &&
                    !HasUnclosedBracket(buffer) &&
                    EndsWithStrongSentenceEnd(stripped)) {
                    buffer.append(stripped.begin(), stripped.end()); // buffer now has new value
                    flush_buffer(); // pushes buffer + clears + resets dialogState
                    continue;
                }

                // 4) First line of new paragraph
                if (buffer.empty()) {
                    // First line – just start a new paragraph (dialog or not)
                    buffer = stripped;
                    dialogState.reset();
                    dialogState.update(stripped);
                    continue;
                }

                // *** DIALOG: treat any line that starts with a dialog opener as a new paragraph

                // 🔸 9a) NEW RULE: If previous line ends with comma,
                //     do NOT flush even if this line starts dialog.
                //     (comma-ending means the sentence is not finished)
                if (BeginsWithDialogOpener(stripped)) {
                    bool shouldFlushPrev = false;

                    if (!buffer.empty()) {
                        // last meaningful char of buffer
                        if (char32_t last{}; TryGetLastNonWhitespace(buffer, last)) {
                            const bool isContinuation =
                                    IsCommaLike(last) ||
                                    IsCjk(last) ||
                                    dialogState.is_unclosed() ||
                                    hasUnclosedBracket;

                            shouldFlushPrev = !isContinuation;
                        }
                    }

                    if (shouldFlushPrev) {
                        segments.push_back(buffer);
                        buffer.clear();
                        // NOTE: we intentionally don't reset dialogState here; we reset below anyway.
                    }

                    // Start (or continue) the dialog paragraph:
                    // C# uses Append even when buffer already has dialog text.
                    buffer.append(stripped.begin(), stripped.end());
                    dialogState.reset();
                    dialogState.update(stripped);
                    continue;
                }


                // 🔸 9b) Dialog end line: ends with dialog closer.
                // Flush when the char before closer is clause/end punctuation,
                // and bracket safety is satisfied (with a narrow OCR/typo override).
                {
                    char32_t lastCh{};

                    if (std::size_t lastIdx{};
                        TryGetLastNonWhitespace(stripped, lastIdx, lastCh) &&
                        IsDialogCloser(lastCh)
                    ) {
                        // Punctuation right before the closer (e.g., “？” / “。”)
                        char32_t prevCh{};
                        const bool punctBeforeCloserIsClauseOrEnd =
                                TryGetPrevNonWhitespace(stripped, lastIdx, prevCh) &&
                                IsClauseOrEndPunct(prevCh);

                        // Snapshot bracket safety BEFORE appending current line
                        const bool bufferHasBracketIssue = hasUnclosedBracket;
                        // your buffer snapshot checker
                        const bool lineHasBracketIssue = HasUnclosedBracket(stripped); // span/view version

                        // Append + update dialog state
                        buffer.append(stripped.begin(), stripped.end());
                        dialogState.update(stripped);

                        // Allow flush if:
                        // - dialog is closed after this line
                        // - punctuation before closer is clause/end
                        // - and either:
                        //     (a) buffer has no bracket issue, OR
                        //     (b) buffer has bracket issue but this line itself is the culprit (OCR/typo)
                        if (!dialogState.is_unclosed() &&
                            punctBeforeCloserIsClauseOrEnd &&
                            (!bufferHasBracketIssue || lineHasBracketIssue)) {
                            flush_buffer();
                        }

                        continue;
                    }
                }
                // Colon + dialog continuation: "她写了一行字：" + "  「如果连自己都不相信……」"
                // if (!buffer_text.empty()) {
                //     if (char32_t last = buffer_text.back(); last == U'：' || last == U':') {
                //         // ignore leading half/full-width spaces for dialog opener
                //         if (std::u32string after_indent = LStrip(stripped);
                //             !after_indent.empty() && Contains(
                //                 DIALOG_OPENERS.data(),
                //                 after_indent[0])
                //         ) {
                //             buffer += stripped;
                //             dialogState.update(stripped);
                //             continue;
                //         }
                //     }
                // }

                // 10) Paragraph boundary flush checks (post-append / buffer-based)
                //
                // NOTE: Dialog safety gate has the highest priority.
                // If dialog quotes/brackets are not closed, never split the paragraph.
                //
                // 10a) Strong / lenient sentence boundary → new paragraph
                //      (level-controlled, requires bracket safety)
                //
                // 10b) Closing CJK bracket boundary → new paragraph
                //      Handles cases where a paragraph ends with a full-width closing
                //      bracket / quote (e.g. ）】》」) and should not be merged with the next line.
                if (!dialogState.is_unclosed() &&
                    (
                        (EndsWithSentenceBoundary(buffer, 2) && !hasUnclosedBracket) ||
                        EndsWithCjkBracketBoundary(buffer)
                    )) {
                    flush_buffer();
                }

                // // 5) Ends with CJK punctuation → new paragraph if not inside unclosed dialog
                // if (!buffer.empty() &&
                //     Contains(CLAUSE_OR_END_PUNCT.data(), buffer.back()) &&
                //     !dialogState.is_unclosed()) {
                //     flush_buffer();
                //     buffer = stripped;
                //     dialogState.update(stripped);
                //     continue;
                // }

                // 7) Indentation -> new paragraph
                // if (IsIndented(raw_line32)) {
                //     flush_buffer();
                //     buffer = stripped;
                //     dialogState.update(stripped);
                //     continue;
                // }

                // 8) Chapter-like endings
                // if (IsChapterEnding(buffer)) {
                //     flush_buffer();
                //     buffer = stripped;
                //     dialogState.update(stripped);
                //     continue;
                // }

                // 9) Default: merge (soft line break)
                buffer += stripped;
                dialogState.update(stripped);
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
