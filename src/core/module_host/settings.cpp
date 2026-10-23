#include "settings.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace hive::extensions {
namespace {
void require(bool condition,const std::string& message) { if (!condition) throw std::runtime_error(message); }
}
void validate_setting(const Setting& s,const Value& value) {
    require(value.index()==s.initial.index(),"Wrong setting type: "+s.id);
    if (const auto* n=std::get_if<int>(&value)) require(*n>=s.minimum && *n<=s.maximum,"Setting outside allowed range: "+s.id);
    if (const auto* str=std::get_if<std::string>(&value)) require(std::find(s.choices.begin(),s.choices.end(),*str)!=s.choices.end(),"Unknown setting choice: "+s.id);
    if (const auto* selected=std::get_if<SettingSelection>(&value)) {
        require(selected->size()<=s.choices.size(),"Too many selected choices: "+s.id);
        std::set<std::string> unique;
        for (const auto& item : *selected) require(unique.insert(item).second
            && std::find(s.choices.begin(),s.choices.end(),item)!=s.choices.end(),"Unknown or duplicate selected choice: "+s.id);
    }
}
}
