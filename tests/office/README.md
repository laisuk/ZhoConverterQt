# OfficeConverter

`src/OfficeConverter.hpp` is a standalone C++20/libzip component. It has no Qt,
OpenCC, FMMSEG, or Jieba dependency. The legacy Minizip implementation remains
unchanged and unused by the Office batch route; other Minizip consumers remain.

Both APIs accept a synchronous `TextConverter`
(`std::function<std::string(std::string_view)>`). Input and output text must be
UTF-8. The callback owns all conversion policy, remains valid for the call, and
runs sequentially. For example, a future caller can supply:

```cpp
TextConverter converter = [&](std::string_view text) {
    auto normalized = normalize(text);
    auto converted = engine(normalized);
    return deToFu(converted);
};
auto result = OfficeConverter::Convert(inputUtf8, outputUtf8, "docx", converter, true);
```

These stage names are illustrative callables, not native helper API declarations.
The application's adapter reuses its existing FMMSEG instance and loaded custom
dictionaries, selected configuration, and punctuation option. Normalization and
DeToFu are not enabled automatically. Font masking remains in OfficeConverter.

Filename conversion belongs exclusively to `BatchWorker::processOneFile()`.
Its existing option, stem conversion with punctuation disabled, output path
generation, and extension handling remain unchanged. The content callback is
never used for filenames.

File conversion opens the input through libzip and copies non-target compressed
entries using ZIP sources. Each target is read, transformed, and appended to a
disk spool; deferred libzip sources reference its individual ranges. Only one
target's text is materialized at a time. The input archive and spool remain alive
until output finalization. Memory conversion shares entry selection and
transformation but retains transformed buffers until the memory ZIP closes.

Target matching follows the current
[Rust reference](https://github.com/laisuk/opencc-fmmseg/blob/master/tools/opencc-rs/src/office_converter/converter.rs),
including the safe-output fix (retrieved October 27, 2026). ZIP separators are
normalized and matching is ASCII case insensitive. Unsafe paths and duplicate
normalized names cause failure; legitimate names containing embedded dots,
Unicode, and spaces remain valid. Interior empty components (`word//file`),
`.` components, traversal, absolute/drive-prefixed paths, and file/directory
aliases are rejected instead of canonicalized or silently skipped. A single
trailing slash still denotes a directory. Collision keys use normalized separators
and exclude a directory's trailing slash; they remain case sensitive, as ZIP names
are case sensitive. All names, including EPUB `mimetype`, pass the same preflight
checks before any text callback. Relationship files are excluded. XLSX worksheets
convert only nonempty text nodes of inline-string cells; shared strings retain whole-entry conversion.
The existing regex-based XML/font behavior remains; this is not XML validation
or a DOM rewrite. Font regex objects are initialized once per format with
thread-safe local statics. Font placeholders use a deterministic prefix absent
from the entire original entry, so literal marker-like text and font values cannot
collide with generated placeholders.

EPUB requires the exact `application/epub+zip` mimetype payload and emits it first,
stored without compression. Directory entries, non-target compression, entry
timestamps, external attributes, file comments, and archive comments are preserved
where supported. Arbitrary extra fields and all original ZIP encoding details are
not guaranteed to survive reconstruction. Non-target compression methods are
set explicitly so stored resources are not implicitly deflated by libzip's
default output policy. Tests compare the original compressed bytes, not just
the decompressed payload, for stored and deflated binary resources.

The modern raw-copy function requires libzip 1.10.0; the existing pre-1.10
fallback remains unchanged. The legacy whole-entry call (`start=0, len=-1`)
automatically selects compressed data, so adding `ZIP_FL_COMPRESSED` is
unnecessary. See the [modern API](https://libzip.org/documentation/zip_source_zip_file/),
[legacy API](https://libzip.org/documentation/zip_source_zip/), and
[libzip 1.9.0 implementation](https://github.com/nih-at/libzip/blob/v1.9.0/lib/zip_source_zip.c).
Both installed SDKs are libzip 1.11.4. Tests exercise its modern conversion route
and its exported legacy raw-source function; an older libzip runtime was not
built or tested. No libzip version requirement was increased.

## Resource limits

Both conversion APIs accept an optional final `uint64_t maxTargetEntryBytes`
argument after `keepFont`. It limits each decompressed **input** XML/XHTML entry,
including its markup. The default is unlimited, preserving existing behavior
for large Office documents. For example, a caller can opt into a 256 MiB cap:

```cpp
auto result = OfficeConverter::Convert(inputUtf8, outputUtf8, "docx", converter,
                                       true, 256ULL * 1024 * 1024);
```

Declared sizes are checked before reading/allocating target text, and every read
is checked against both the declared size and the configured cap before append.
Missing, overstated/truncated, or understated sizes fail through libzip checks or
the explicit size checks. The EPUB mimetype read has its own fixed 20-byte bound.
Spool length/offset conversions and validation byte counts are checked without
overflow; output validation continues to use fixed-size buffers.

This cap is not a complete ZIP bomb defense. With the default, a single large
target can still exhaust memory. There is no aggregate archive size, entry-count,
compression-ratio, disk-space, or time budget. Non-target validation still
decompresses every resource; archive catalogs consume memory. Callback output
expansion, regex processing, and the memory-backed ZIP also have resource costs
outside the input cap. Callers handling untrusted archives should choose a cap
and apply process-level resource budgets as appropriate.

Output and spool files are allocated exclusively with unique names beside the
destination. RAII removes owned files on ordinary failures. The output is fully
closed, reopened with consistency checks, and every entry is drained with bounded
buffers to check decompression, size, and CRC before replacement. Callback
exceptions, invalid UTF-8, malformed archives, and replacement failures return
failure results without committing output. Same-file paths and existing hardlink
or symlink aliases are rejected through filesystem equivalence.

Unix uses same-directory filesystem rename with atomic directory-entry
replacement. Windows uses `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`, without a
general atomicity guarantee. Neither path deletes the destination first, changes
read-only attributes, or falls back to copying. Concurrent jobs use separate
temporary files; competing replacements of the same destination can fail due to
Windows sharing rules. There is no fsync/power-loss durability guarantee. Cleanup
is best effort and does not cover abrupt termination. Paths passed to the public
API are UTF-8; Windows filesystem and libzip calls use wide paths.

## Tests

Standalone, without Qt (supply your compiler and libzip SDK prefix as needed):

```sh
cmake -S tests/office -B build/office-tests -DCMAKE_PREFIX_PATH=/path/to/libzip
cmake --build build/office-tests
ctest --test-dir build/office-tests --output-on-failure
```

Within the application build, configure `-DZHO_BUILD_OFFICE_TESTS=ON`, build
`office_converter_tests` and `office_batch_tests`, then run
`ctest --test-dir <build> -R office --output-on-failure`.

Core tests cover seven formats, memory/file equivalence, composed mock stages,
XLSX formulas and empty text, PPTX extended parts, font masking, EPUB ordering,
invalid/truncated/CRC-damaged ZIPs, unsafe names, UTF-8, callback errors, validation
and replacement failures, ordinary cleanup, concurrent jobs, same-file aliases,
Unicode paths, Windows read-only destinations, and Windows sharing failures.
Batch tests exercise the real worker with FMMSEG custom dictionaries, punctuation,
both filename-option states, fonts, progress/logs, and cancellation. Hardening
regressions cover ambiguous/duplicate normalized paths (including EPUB),
stored/deflated raw bytes and metadata, literal font-marker collisions, exact/zero
input caps, forged declared sizes, non-target exemption from the cap, and bounded
mimetype reads. Invalid-path and size-limit failures preserve destinations and
clean temporary files through the file-backed API.

Windows verification: MinGW application build and Office core/batch tests; MSVC
standalone core build/tests and compiler static analysis. Linux, macOS, and a
manual interactive GUI session were not exercised.

During the earlier refactor verification, the existing `batch_encoding` suite
stalled during startup and was stopped. It was not rerun for this hardening pass
and is not counted as passing. The dedicated Office BatchWorker integration
test passed independently.

MSVC core tests passed, including five consecutive diagnostic runs, but some
other invocations failed when Windows denied creation of the existing hard-link
fixture. The direct Windows API also returned access denied and the source was
not read-only. The cause remains unresolved; diagnostic probes were removed and
the hard-link assertion was left intact. The final uninstrumented MSVC run also
failed at that fixture. The new hardening checks execute before it and passed,
but the full MSVC suite must not be reported as passing.
