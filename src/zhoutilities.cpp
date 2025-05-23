#include <string>
#include <zhoutilities.h>
#include "opencc_fmmseg_capi.h"
#include "boost/regex.hpp"
#include "boost/nowide/convert.hpp"

int ZhoCheck(const std::string &test_text) {
    const auto opencc = opencc_new();
    const int code = opencc_zho_check(opencc, test_text.c_str());
    if (opencc != nullptr) {
        opencc_delete(opencc);
    }
    return code;
}

size_t find_max_utf8_length(const std::string_view sv, size_t max_byte_count) {
    // 1. No longer than max byte count
    if (sv.size() <= max_byte_count) {
        return sv.size();
    }
    // 2. Longer than byte count
    while ((sv[max_byte_count] & 0b11000000) == 0b10000000) {
        --max_byte_count;
    }
    return max_byte_count;
}

std::string convert_punctuation(const std::string_view sv, const std::string_view config) {
    std::unordered_map<std::wstring, std::wstring> s2t_punctuation_chars = {
        // Declare a dictionary to store the characters and their mappings
        {L"“", L"「"},
        {L"”", L"」"},
        {L"‘", L"『"},
        {L"’", L"』"}
    };

    const std::wstring input_text = utf8_to_wstring(sv.data());
    std::wstring output_text;
    // Use the join method to create the regular expression patterns
    if (config.substr(0, 1) == "s") {
        // Create the regular expression patterns
        std::wstring s2tPattern = L"[";
        for (const auto &pair: s2t_punctuation_chars) {
            s2tPattern += pair.first;
        }
        s2tPattern += L"]";
        const boost::wregex s2t_regex(s2tPattern);
        output_text = boost::regex_replace(input_text, s2t_regex,
                                           [&](const boost::wsmatch &m) {
                                               return s2t_punctuation_chars[m.str(0)];
                                           });
    } else {
        // Use the map to reverse the dictionary
        std::unordered_map<std::wstring, std::wstring> t2s_punctuation_chars;
        for (const auto &pair: s2t_punctuation_chars) {
            t2s_punctuation_chars[pair.second] = pair.first;
        }
        std::wstring t2sPattern(L"[");
        for (const auto &pair: t2s_punctuation_chars) {
            t2sPattern += pair.first;
        }
        t2sPattern += L"]";
        const boost::wregex t2sRegex(t2sPattern);
        output_text = boost::regex_replace(input_text, t2sRegex,
                                           [&](const boost::wsmatch &m) {
                                               return t2s_punctuation_chars[m.str(0)];
                                           });
    }
    return wstring_to_utf8(output_text);
}

std::wstring utf8_to_wstring(const std::string &utf8_text) {
    return boost::nowide::widen(utf8_text);
}

std::string wstring_to_utf8(const std::wstring &wide_text) {
    return boost::nowide::narrow(wide_text);
}
