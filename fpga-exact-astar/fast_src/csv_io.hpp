#pragma once

#include <string>
#include <utility>

namespace srb {

std::string trim(std::string text);
std::pair<std::string, std::string> parse_csv_pair(const std::string& line);

}  // namespace srb

