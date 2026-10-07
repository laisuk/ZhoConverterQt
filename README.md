# ZhoConverterQt
## Batch text encoding

Settings → **Auto-Detect CJK Text Encoding (Batch)** appears below Convert
Filename and is saved between runs (default off). Enable it to detect Big5,
GB18030 and Shift-JIS using the same detector and decoder as the Main editor.
Unicode BOM detection works with either setting. Batch text output is UTF-8;
files whose encoding cannot be detected or decoded are skipped with an error.
Short inputs can be ambiguous. PDF and Office processing are unaffected.

For encoding integration tests, configure with `-DZHO_BUILD_TESTS=ON`, build,
and run `ctest --test-dir <build-directory> --output-on-failure`.
