# uchardet trimmed for ZhoConverterQt

Small subset of uchardet 0.0.5 for interactive Chinese legacy-encoding fallback detection.

Included: Big5 and GB18030 (the GB2312/GBK/GB18030 family). UTF-8/UTF-16, Japanese,
Korean, EUC-TW, single-byte language models, the universal detector and the public
`uchardet_*` C API are intentionally omitted.

Recommended order in `EncodingDetector`: BOM -> ASCII/strict UTF-8 -> UTF-16 heuristic
-> `uchardet_trimmed::detectChineseLegacy()` -> Unknown/manual fallback.

For interactive loading, feed a sample such as the first 128 KiB. Batch conversion
should remain strict UTF-8 and should not use this detector.

The Mozilla-derived source files and frequency tables retain their original license
headers. Upstream `COPYING` is included.
