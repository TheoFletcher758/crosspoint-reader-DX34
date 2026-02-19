#pragma once

#include <optional>
#include <string>

namespace BookProgress {

std::optional<int> getPercent(const std::string& path);
std::string getPrefix(const std::string& path);
std::string withPrefix(const std::string& path, const std::string& title);

}  // namespace BookProgress

