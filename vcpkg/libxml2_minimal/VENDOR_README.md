# libxml2 minimal vendor snapshot

Upstream: libxml2 2.15.4 (source archive supplied by project maintainer).
This is a **conservative source-tree trim**, not a modified XML engine.

Retained: upstream CMake build, root C implementation files, public/private
headers, generated codegen .inc files, Windows resource, build templates,
Copyright and README. Removed: upstream tests, expected results, fuzzing,
Python bindings, examples, docs, autotools/meson packaging, CI, utilities
not needed for a static library build. Upstream CMake remains unchanged.

Use CMake with LIBXML2_WITH_PROGRAMS=OFF, LIBXML2_WITH_TESTS=OFF,
LIBXML2_WITH_DOCS=OFF, LIBXML2_WITH_PYTHON=OFF and BUILD_SHARED_LIBS=OFF.
Optional features should be disabled as needed in the consuming project.

License: see Copyright. Keep this notice and Copyright when redistributing.

No DOCX/ODT/EPUB integration or cross-platform build has been verified here.
