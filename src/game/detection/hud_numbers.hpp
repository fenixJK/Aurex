#pragma once
#include "hive/game/detection/perception.hpp"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <regex>
namespace hive::game::detail {
inline bool inside(const Rect& r,const Frame& frame) {
    return r.valid() && r.x>=0 && r.y>=0 && r.x+r.width<=frame.width && r.y+r.height<=frame.height;
}
inline std::string normalize(const std::string& input) {
    std::string out;
    for (unsigned char c : input) {
        if (std::isspace(c)) { if (!out.empty() && out.back()!=' ') out+=' '; }
        else out+=static_cast<char>(std::tolower(c));
    }
    if (!out.empty() && out.back()==' ') out.pop_back();
    return out;
}
inline std::optional<std::uint64_t> amount(std::string input,std::uint64_t limit=1000000000000000ULL) {
    if (input.size()>24) return {};
    static const std::regex grouped(R"((0|[1-9][0-9]*|[1-9][0-9]{0,2}(,[0-9]{3})+))");
    if (!std::regex_match(input,grouped)) return {};
    std::erase(input,',');
    std::uint64_t value{}; const auto result=std::from_chars(input.data(),input.data()+input.size(),value);
    if (result.ec!=std::errc{} || result.ptr!=input.data()+input.size() || value>limit) return {};
    return value;
}
}
