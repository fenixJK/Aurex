#pragma once
#include "game/actions/travel/checkpoint.hpp"
#include <filesystem>
#include <map>
#include <memory>
#include <variant>

namespace hive::movement {
using Parameters=std::map<std::string,std::variant<bool,int,double,std::string>>;
struct ShiftLock { bool enabled{}; };
struct Marker { std::string name; };
using Step=std::variant<workflows::CheckpointPlan,ShiftLock,Marker>;
class Recipes {
public:
    explicit Recipes(std::map<std::string,std::string> sources);
    ~Recipes();
    std::vector<Step> sequence(const std::string& file,const Parameters& parameters,double speed) const;
    std::vector<workflows::CheckpointPlan> path(const std::string& file,const Parameters& parameters,double speed) const;
    std::vector<game::WalkLeg> legs(const std::string& file,const Parameters& parameters={}) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class Library {
public:
    explicit Library(std::filesystem::path directory);
    std::shared_ptr<const Recipes> snapshot() const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
using Source=std::shared_ptr<const Library>;
std::shared_ptr<const Recipes> snapshot(const Source& source={});
}
