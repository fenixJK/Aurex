#pragma once
#include "program.hpp"
#include "hive/game/detection/conversation.hpp"

namespace hive::automation {
std::unique_ptr<Program> converse(game::PromptLandmark prompt,std::shared_ptr<const game::ConversationSource> source);
}
