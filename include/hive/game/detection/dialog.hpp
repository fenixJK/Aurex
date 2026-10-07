#pragma once
#include "landmarks.hpp"

namespace hive::game {
struct DialogLabels { std::string positive{"Yes"},negative{"No"}; };
Observation<DialogButtons> read_confirmation(const Frame& frame,std::uint64_t generation);
Observation<DialogButtons> read_dialog(const Context& context,const Reading<TextSnapshot>& text,const PromptLandmark& question,const DialogLabels& labels={});
}
