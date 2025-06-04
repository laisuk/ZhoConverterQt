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
    const std::unordered_map<std::wstring, std::wstring> s2t_mapping = {
        // Declare a dictionary to store the characters and their mappings
        {L"“", L"「"},
        {L"”", L"」"},
        {L"‘", L"『"},
        {L"’", L"』"}
    };

    const std::unordered_map<std::wstring, std::wstring> t2s_mapping = {
        // Declare a dictionary to store the characters and their mappings
        {L"「", L"“"},
        {L"」", L"”"},
        {L"『", L"‘"},
        {L"』", L"’"}
    };

    const std::wstring input_text = utf8_to_wstring(sv.data());

    std::wstring pattern = L"[";
    std::unordered_map<std::wstring, std::wstring> mapping;
    // Use the join method to create the regular expression patterns
    if (config.substr(0, 1) == "s") {
        // Create the regular expression patterns
        for (const auto &[key, val]: s2t_mapping) {
            pattern += key;
        }
        mapping = s2t_mapping;
    } else {
        for (const auto &[key, val]: t2s_mapping) {
            pattern += key;
        }
        mapping = t2s_mapping;
    }
    pattern += L"]";

    const boost::wregex regex(pattern);
    const std::wstring output_text = boost::regex_replace(input_text, regex,
                                                          [&](const boost::wsmatch &m) {
                                                              return mapping[m.str(0)];
                                                          });
    return wstring_to_utf8(output_text);
}

std::wstring utf8_to_wstring(const std::string &utf8_text) {
    return boost::nowide::widen(utf8_text);
}

std::string wstring_to_utf8(const std::wstring &wide_text) {
    return boost::nowide::narrow(wide_text);
}
