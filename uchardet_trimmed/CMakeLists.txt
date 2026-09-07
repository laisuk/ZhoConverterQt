add_library(uchardet_trimmed STATIC
        uchardet_trimmed/uchardet/ChineseLegacyDetector.cpp
        uchardet_trimmed/uchardet/CharDistribution.cpp
        uchardet_trimmed/uchardet/nsBig5Prober.cpp
        uchardet_trimmed/uchardet/nsGB2312Prober.cpp
        uchardet_trimmed/uchardet/nsMBCSSM.cpp
)

target_include_directories(uchardet_trimmed PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/uchardet_trimmed/uchardet
)

target_compile_features(uchardet_trimmed PUBLIC cxx_std_17)
