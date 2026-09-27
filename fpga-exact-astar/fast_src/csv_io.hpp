#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace srb {

std::string trim(std::string text);
std::pair<std::string, std::string> parse_csv_pair(const std::string& line);
std::pair<std::string_view, std::string_view> parse_csv_pair_view(
    std::string_view line);

}  // namespace srb
