#include "csv_io.hpp"

#include <cctype>
#include <stdexcept>

namespace srb {

std::string trim(std::string text) {
    size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) --last;
    return text.substr(first, last - first);
}

std::pair<std::string, std::string> parse_csv_pair(const std::string& line) {
    const size_t first = line.find(',');
    if (first == std::string::npos) throw std::runtime_error("CSV row has fewer than two columns");
    const size_t second = line.find(',', first + 1);
    const std::string from = trim(line.substr(0, first));
    const std::string to = trim(line.substr(
        first + 1, second == std::string::npos ? second : second - first - 1));
    if (from.empty() || to.empty()) throw std::runtime_error("CSV row has an empty From or To");
    return {from, to};
}

}  // namespace srb

