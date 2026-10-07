#pragma once
#include "state.hpp"

namespace hive::game {
enum class PromptMatch { exact, phrase };
inline constexpr float hive_prompt_confidence=.5f;
struct PromptLandmark {
    std::vector<std::string> phrases;
    Rect region{0.2,0,0.6,0.3};
    float confidence{0.8f};
    PromptMatch match{PromptMatch::exact};
    bool ignore_unrelated_uncertainty{};
};
void validate(const PromptLandmark& landmark);
std::optional<Rect> interaction_text_region(const Frame& frame);
Frame interaction_text_image(const Frame& frame,Rect region);
Reading<bool> read_prompt_landmark(const Context& context,const Reading<TextSnapshot>& text,
    const PromptLandmark& landmark);
PromptLandmark hive_proximity_prompt();
PromptLandmark hive_claim_prompt();
}
