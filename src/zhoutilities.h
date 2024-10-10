//
// Created by user on 2/4/2024.
//
#ifndef ZHOUTILITIES_H
#define ZHOUTILITIES_H

#include <string_view>

int ZhoCheck(const std::string &test_text);

size_t find_max_utf8_length(std::string_view sv, size_t max_byte_count);

std::string convert_punctuation(std::string_view sv, std::string_view config);

#endif // ZHOUTILITIES_H
