#pragma once
#include "results.hpp"

namespace hive::features::tools {
struct MutationType { std::string name; bool flat{},percent{}; };
const std::vector<MutationType>& mutation_types();
struct Mutation { std::string name; double value{}; bool percent{}; };
struct MutationRule { std::string name; double minimum{}; bool percent{}; };
using MutationFilter=std::vector<MutationRule>;
void validate(const MutationFilter& filter);
bool matches(const Mutation& mutation,const MutationFilter& filter);
std::string describe(const Mutation& mutation);
struct MutationPanel { Mutation mutation; std::optional<Point> close; bool bee_menu{}; };
Observation<MutationPanel> read_mutation(const ObservationContext&,const game::State&);
}
