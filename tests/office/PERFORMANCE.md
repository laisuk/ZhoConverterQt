# OfficeConverter performance and safety audit

Measured on October 31, 2026. Baseline: working-tree `src/OfficeConverter.hpp`
at HEAD `5d0ccf3`, byte-identical to the requested historical header at
`e7361c9d63762cfab176e25390edb5c6ca393363` (`git diff` was empty).
Existing unrelated working-tree files were left alone.

## Audit and decisions

| Area | Finding and decision |
| --- | --- |
| `validate()` | Draining every finalized entry is necessary for the existing strict guarantee. Raw copying can carry a corrupt resource into the output. Keep EOF reads, decompression/CRC checks, declared-size checks and close checks, with bounded buffers. |
| `ZIP_CHECKCONS` | Complementary structural protection, retained on input and finalized output. It does not replace reading payloads for CRC verification. No structural-only mode or new public option was added. |
| `ConvertBytes()` | The finalized memory source exposes its actual archive size. Reserve that capacity after checking `ZIP_STAT_SIZE` and `vector::max_size()`. Fall back to existing growth if size information is unavailable. Keep EOF/error handling and strict output validation. This avoids repeated vector allocations and copying on large binary-dominated outputs. |
| `addBuffer()` | The existing malloc/copy is retained. libzip reads sources during finalization and frees owned buffer sources with `free`; passing a temporary string's storage is unsafe. A custom source callback or archive-scoped string owner would add lifetime/error-path complexity for an unproven benefit. File conversions already spool transformed text rather than retaining all buffers. Added the direct `<cstdlib>` include. |
| Entry names | Preflight intentionally checks every name before invoking callbacks. Retain the second normalization rather than storing a second archive-wide name catalog; it trades small repeated work for bounded per-entry temporary storage. Lowercasing already occurs once per processing pass. Cache the target boolean locally, avoiding its second evaluation when preserving compression. No security checks or matching rules change. |
| `validUtf8()` | Keep both scans. Input validation protects callback assumptions; output validation checks arbitrary custom callbacks. Neither scan proves the other unnecessary. No parser/SIMD rewrite. |
| `maskFont()` | Regexes are already cached once per format. Search for the first match before scanning for a collision-free prefix or allocating/rebuilding the entire XML. Return immediately if absent; reuse that first match otherwise. Keep the map, marker order and repeated restoration behavior: changing these could alter callbacks that duplicate/reorder markers, or restoration semantics. |
| Unchanged resources | Already copied as compressed ZIP sources, including the pre-1.10 fallback. Retain explicit original compression settings, metadata copying, input lifetime and spool lifetime. No decompression/recompression optimization was needed here. |
| Target reads | Do not reserve from untrusted declared input sizes. Incremental bounded reads retain the existing allocation behavior for forged headers; the new output reserve uses a different, finalized internal source. |

The public APIs, header-only C++20 architecture, result structures, target-size
limits, seven supported formats, EPUB mimetype path and transactional replacement
sequence are unchanged. There are no new third-party dependencies or dependency
version/configuration changes. The standalone CMake file adds only an explicitly
built benchmark target and a benchmark-only alternate-header setting.

libzip documents [stricter consistency checks](https://libzip.org/documentation/zip_open/),
[source stat validity fields](https://libzip.org/documentation/zip_source_stat/),
and [buffer source lifetime requirements](https://libzip.org/documentation/zip_source_buffer/).
The CRC guarantee is also covered by the target and non-target corruption tests;
structural validation alone is explicitly weaker than strict validation.

## Benchmark method

Driver: `office_converter_benchmark.cpp`; full observations:
[`performance-results.csv`](performance-results.csv). Windows x64, GCC 16.2.0,
CMake Release (`-O3 -DNDEBUG`), bundled static libzip 1.11.4/zlib 1.3.1. Both
executables use identical compiler/dependency settings and the exact same driver,
one with a saved baseline header. Workload payloads are deterministic; input entry
timestamps and random temporary paths can vary. Each workload/version runs in a
fresh process, before then after per workload. One warm-up precedes 11 measured
iterations. Reported times are medians, in milliseconds; no confidence intervals.

These are synthetic DOCX-shaped ZIPs, not a corpus of real Office documents.
They isolate storage/text/catalog costs with a cheap callback replacing the first
three ASCII bytes, not actual Chinese conversion. Regression tests exercise all
seven formats separately. Workloads:

- Small: 8 KiB text, no resource.
- Medium: 128 KiB text, 4 MiB stored pseudorandom resource.
- Large: 8 KiB text, 32 MiB stored pseudorandom resource.
- Deflated: large workload with the resource deflated (poorly compressible).
- Text: 2 MiB compressible text, no resource, `keepFont=false`.
- Catalog: 8 KiB text and 2,000 tiny resource entries.

All other cases set `keepFont=true` without matching font attributes, exercising
the no-match path. Existing regression fixtures cover matching fonts and marker
collisions. These measurements do not establish a speedup for font-heavy inputs
or isolate the small predicate/font changes from the capacity reservation.

| Workload | File total before → after (ms) | Memory total before → after (ms) | Peak process before → after (KiB) |
| --- | ---: | ---: | ---: |
| Small | 1.615 → 1.576 | 0.385 → 0.382 | 5,976 → 6,344 |
| Medium | 13.022 → 13.373 | 10.258 → 9.117 | 23,520 → 18,412 |
| Large | 63.105 → 65.413 | 55.593 → 41.037 | 138,244 → 104,556 |
| Deflated | 53.392 → 52.138 | 44.702 → 30.249 | 138,268 → 104,668 |
| Text | 13.445 → 13.735 | 11.317 → 11.604 | 12,120 → 12,124 |
| Catalog | 57.701 → 57.224 | 8.628 → 8.454 | 9,424 → 9,380 |

File-backed phase measurements use the same private pipeline through the existing
test friend, without adding production timing hooks. Conversion includes preflight,
callback and spool writes. Finalization is `zip_close`. Validation includes reopen
with `ZIP_CHECKCONS`, full draining and closing the verification archive.

| Workload | Conversion before → after (ms) | Finalization before → after (ms) | Validation before → after (ms) |
| --- | ---: | ---: | ---: |
| Small | 0.381 → 0.378 | 0.606 → 0.605 | 0.077 → 0.079 |
| Medium | 3.756 → 3.735 | 6.926 → 6.805 | 2.119 → 2.166 |
| Large | 0.415 → 0.427 | 45.046 → 48.774 | 16.163 → 16.186 |
| Deflated | 0.425 → 0.411 | 32.950 → 33.930 | 17.982 → 17.577 |
| Text | 4.080 → 4.129 | 7.285 → 7.274 | 1.394 → 1.678 |
| Catalog | 2.648 → 2.385 | 30.804 → 30.589 | 14.628 → 14.718 |

Public API totals are measured in separate full calls, including setup, cleanup,
and replacement (file API) or output extraction/validation (memory API). Therefore
the independent phase medians do not sum to the total. Peak memory is Windows
`PeakWorkingSetSize` for the entire process, including fixture generation and both
APIs; it is neither an isolated converter allocation count nor a median peak.
Large cases show approximately 24% lower process peak and 26–32% faster memory
conversion. File timings and small/text cases are mixed; no general file-speed
claim is warranted. Filesystem caching/scanning and scheduler noise remain factors.

An initial exploratory benchmark failed replacing a repeatedly reused destination
(Windows error 5). The recorded driver gives each iteration a new destination;
both recorded versions use this same driver. Existing-destination replacement
remains covered by regression tests. No failed trial is included in the CSV.

## Reproducing

Save the baseline header outside `src` before editing. Configure two build trees
using identical Release, compiler and libzip-prefix arguments. For the baseline
only, add `-DOFFICE_BENCHMARK_HEADER=/absolute/path/to/baseline.hpp`. Build
`office_converter_benchmark` explicitly, then run each executable with
`<small|medium|large|deflated|text|catalog> 11`. CSV columns emitted are workload,
conversion ms, finalization ms, validation ms, public file total ms, public memory
total ms, and peak process KiB. Run in a writable working directory. No external
fixtures or network are needed. Linux/macOS use `getrusage` for process peak RSS;
those paths were not executed in this audit.

On Windows, put the selected compiler's runtime directory before Qt's directory
on `PATH` (in this run, `C:/mingw64/bin` before `C:/Qt/6.11.2/mingw_64/bin`).

## Regression coverage and remaining trade-offs

Existing tests cover normal seven-format conversions, file/memory payload
equivalence, malformed/truncated ZIPs, duplicate/unsafe names, invalid input and
callback UTF-8, target CRC errors, forged entry sizes and caps, callbacks, font
preservation, metadata and compressed-byte preservation, EPUB rules, cleanup,
destination safety, Unicode paths, sharing/read-only failures and concurrency.
New checks add 4 MiB stored/deflated binary resources through both APIs, no-font
matching with `keepFont`, callback output expansion/contraction/empty output,
well-formed empty ZIPs, non-target CRC rejection through the memory API, and
non-standard callback exceptions through both APIs.

Final verification on Windows:

- Baseline standalone MinGW core suite: passed before the production edits.
- Expanded MinGW Release core suite: passed (0.24 s).
- Expanded MSVC Release core suite: passed (0.31 s); build with the existing
  `/analyze` setting passed without diagnostics.
- MinGW Release `ZhoConverterQt` application build: passed.
- MinGW Release Office BatchWorker integration build and test: passed (0.06 s),
  including real FMMSEG settings/custom dictionaries, fonts, filenames and cancellation.
- `git diff --check`: passed. Linux/macOS and interactive GUI behavior were not run.

Sandboxed core runs failed at the existing hard-link fixture with Windows access
denied. The unchanged fixture passed in both compilers when the suites ran outside
the filesystem sandbox. A first integration run crashed in `std::locale` / iostream
initialization: GDB showed Qt's older `libstdc++-6.dll` being loaded. Putting the
GCC 16.2 runtime first on `PATH` made the integration suite pass. No test assertions
were skipped or weakened to accommodate these environment failures.

Strict validation still costs O(total decompressed output), intentionally. Font
regexes and per-marker restoration can remain expensive with many matches; this
audit does not justify a semantic rewrite. The in-memory API still retains the
input, libzip buffers, transformed allocations and returned ZIP concurrently.
There is no aggregate resource budget, and callbacks can expand output without
bound. Existing Windows replacement and crash/power-loss limitations remain as
documented in `README.md`.
