# Changelog

All notable changes to this project will be documented in this file.

This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html) and uses
the [Keep a Changelog](https://keepachangelog.com/en/1.0.0/) format.

---

## [1.0.0] - Unreleased

**ZhoConverterQt** is a feature-rich, cross-platform Simplified and Traditional Chinese conversion application powered by
`opencc-fmmseg C API`, featuring customizable dictionaries, Unicode text processing, intelligent encoding detection, and
comprehensive document conversion support.

### Added

- Initial public release of ZhoConverterQt, a cross-platform Chinese text conversion application built with Qt and C++.
- Simplified and Traditional Chinese conversion using `opencc-fmmseg`, including regional conversion configurations.
- Single-text and batch conversion interfaces.
- Custom dictionary management with support for dynamic dictionary loading and configuration.
- Text file loading with automatic CJK encoding detection and manual encoding selection.
- PDF text extraction using PDFium.
- EPUB text extraction using libzip and libxml2.
- DOCX and ODT text extraction using libzip and libxml2, including document structure, tables, lists, and numbering.
- DOCX extraction of footnotes, endnotes, comments, headers, and footers.
- Source and destination text editors with character counting and configurable fonts.
- File conversion and output filename conversion support.

---