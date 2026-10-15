#pragma once
#include "state.hpp"

namespace hive::game {
struct HudFeatures;
enum class PromptMatch { exact, phrase, prefix };
inline constexpr float hive_prompt_confidence=.5f;
inline constexpr int interaction_text_padding=4;
inline constexpr Rect interaction_prompt_search_region{.2,0,.6,.3};
struct PromptLandmark {
    std::vector<std::string> phrases;
    Rect region{interaction_prompt_search_region};
    float confidence{0.8f};
    PromptMatch match{PromptMatch::exact};
    bool ignore_unrelated_uncertainty{};
};
void validate(const PromptLandmark& landmark);
std::optional<Rect> interaction_prompt_region(const Frame& frame);
std::optional<Rect> interaction_text_region(const Frame& frame,const HudFeatures* hud=nullptr);
Frame interaction_text_image(const Frame& frame,Rect region);
std::optional<TextLine> interaction_label(const Frame& image);
Reading<bool> read_prompt_landmark(const Context& context,const Reading<TextSnapshot>& text,
    const PromptLandmark& landmark);
PromptLandmark hive_proximity_prompt();
PromptLandmark hive_claim_prompt();
}
