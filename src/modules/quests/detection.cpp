#include "hive/modules/quests/detection.hpp"
#include "hive/game/detection/menus.hpp"
#include "hive/modules/quests/catalog.hpp"
#include <algorithm>
#include <cctype>
#include <tuple>
#include "pixels.hpp"

namespace hive::features::quests {
namespace {
bool identifier(const std::string& text) {
    return !text.empty() && text.size()<=80 && std::none_of(text.begin(),text.end(),[](unsigned char c) { return c<32 || c==127; });
}
void validate(Rect area) {
    if (!area.valid() || area.x<0 || area.y<0 || area.x+area.width>1 || area.y+area.height>1)
        throw std::invalid_argument("Invalid normalized quest region");
}
bool overlaps(Rect a,Rect b) {
    return a.x<b.x+b.width && b.x<a.x+a.width && a.y<b.y+b.height && b.y<a.y+a.height;
}
std::string normalized(const std::string& text) {
    std::string result;
    for (unsigned char c : text) {
        if (std::isspace(c)) { if (!result.empty() && result.back()!=' ') result+=' '; }
        else result+=static_cast<char>(std::tolower(c));
    }
    if (!result.empty() && result.back()==' ') result.pop_back(); return result;
}
struct Text { std::string value; bool clipped{},uncertain{}; };
Text text(const game::DetectionInput& input,Rect area,float minimum_confidence=.8f) {
    area={area.x*input.frame.width,area.y*input.frame.height,area.width*input.frame.width,area.height*input.frame.height};
    std::vector<const TextLine*> selected; Text result;
    for (const auto& line : input.text) {
        if (!line.pixels.valid()) continue;
        if (!overlaps(line.pixels,area)) continue;
        if (line.pixels.x<area.x || line.pixels.y<area.y || line.pixels.x+line.pixels.width>area.x+area.width
            || line.pixels.y+line.pixels.height>area.y+area.height) { result.clipped=true; continue; }
        if (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<minimum_confidence || *line.confidence>1)) {
            result.uncertain=true; continue;
        }
        selected.push_back(&line);
    }
    std::stable_sort(selected.begin(),selected.end(),[](const TextLine* a,const TextLine* b) {
        return std::tie(a->pixels.y,a->pixels.x)<std::tie(b->pixels.y,b->pixels.x);
    });
    for (const auto line : selected) {
        if (result.value.size()+line->text.size()+1>512) { result.clipped=true; break; }
        if (!result.value.empty()) result.value+=' '; result.value+=line->text;
    }
    return result;
}
class QuestDetector final : public game::Detector {
    std::shared_ptr<const LayoutSource> layout_;
    bool desktop_{};
    mutable std::optional<Rect> reading_area_,menu_;
    mutable ObservationContext layout_context_;
    mutable Time discovered_{};
    struct VisibleCard { CardRegion region; Card card; std::uint64_t title; };
    mutable std::vector<VisibleCard> visible_;
    mutable std::optional<BrowserScene> scene_;
    mutable int width_{},height_{};
    static std::uint64_t title_pixels(const Frame& frame,Rect area) {
        std::uint64_t hash=14695981039346656037ULL;
        unsigned ink=0,pixels=0;
        for (int y=std::max(0,int(area.y*frame.height));y<std::min(frame.height,int(std::ceil((area.y+area.height)*frame.height)));++y)
            for (int x=std::max(0,int(area.x*frame.width));x<std::min(frame.width,int(std::ceil((area.x+area.width)*frame.width)));++x) {
                const auto i=(std::size_t(y)*frame.width+x)*4;
                const bool dark=frame.rgba[i]<90 && frame.rgba[i+1]<90 && frame.rgba[i+2]<90;
                ink+=dark; ++pixels; hash^=dark;
                hash*=1099511628211ULL;
            }
        return ink>=16 && ink<pixels*.9?hash:0;
    }
    bool same_cards(const Frame& frame,std::uint64_t generation) const {
        return scene_ && !visible_.empty() && frame.width==width_ && frame.height==height_
            && generation==layout_context_.generation && frame.window.id==layout_context_.window.id
            && frame.window.process==layout_context_.window.process && frame.window.bounds==layout_context_.window.bounds
            && std::all_of(visible_.begin(),visible_.end(),[&](const auto& card) {
                return card.title && std::all_of(card.card.objectives.begin(),card.card.objectives.end(),[](const auto& row) { return row.known; })
                    && title_pixels(frame,card.region.title)==card.title;
            });
    }
public:
    QuestDetector(std::shared_ptr<const LayoutSource> layout,bool desktop=false):layout_(std::move(layout)),desktop_(desktop) {}
    std::vector<Rect> text_regions(const Frame& frame,std::uint64_t generation) const override {
        if (!desktop_) return {{0,0,1,1}};
        for (const auto& control:game::menu_controls({frame,{},generation,{}})) if (control.tab==game::MenuTab::quests) {
            if (control.selected==false) { visible_.clear(); scene_.reset(); return {}; }
            if (control.selected!=false && same_cards(frame,generation)) return {};
            if (reading_area_ && menu_ && control.bounds==*menu_ && layout_context_.generation==generation
                && layout_context_.window.id==frame.window.id && layout_context_.window.process==frame.window.process
                && layout_context_.window.bounds==frame.window.bounds && frame.captured>=discovered_
                && frame.captured-discovered_<Time(5000)) return {*reading_area_};
        }
        return {{0,0,1,1}};
    }
    std::shared_ptr<const game::Detection> detect(const game::DetectionInput& input) const override {
        if (desktop_ && same_cards(input.frame,input.generation)) {
            const auto menus=game::menu_controls(input);
            if (std::any_of(menus.begin(),menus.end(),[](const auto& menu) { return menu.tab==game::MenuTab::quests && menu.selected!=false; })) {
                auto result=std::make_shared<Cards>(); result->browser=scene_;
                for (const auto& remembered:visible_) {
                    auto card=remembered.card;
                    for (std::size_t i=0;i<card.objectives.size();++i) {
                        auto& row=card.objectives[i];
                        const auto state=progress_state(input.frame,remembered.region.rows[i].area);
                        row.known=row.known && state!=ProgressColor::unknown;
                        if (row.known) row.collected=state==ProgressColor::complete?row.required:0;
                    }
                    result->values.push_back({Knowledge::present,std::move(card),input.frame.captured,input.frame.window,input.generation});
                }
                result->browser->cards=result->values;
                return result;
            }
        }
        visible_.clear(); scene_.reset();
        const auto regions=layout_->locate(input);
        if (regions.size()>16) throw std::invalid_argument("Too many quest card regions");
        auto cards=std::make_shared<Cards>(); std::set<std::string> givers; std::vector<Rect> occupied;
        for (const auto& region : regions) {
            if (input.cancelled && input.cancelled()) return {};
            if (!givers.insert(region.giver).second) throw std::invalid_argument("Ambiguous quest giver regions");
            std::vector<Rect> areas{region.title};
            for (const auto& row : region.rows) areas.push_back(row.area);
            for (const auto area : areas) {
                validate(area);
                if (std::any_of(occupied.begin(),occupied.end(),[&](Rect other) { return overlaps(area,other); }))
                    throw std::invalid_argument("Overlapping quest cards");
            }
            occupied.insert(occupied.end(),areas.begin(),areas.end());
            auto observation=read_card(input,region);
            if (observation.knowledge==Knowledge::present) {
                if (desktop_) visible_.push_back({region,observation.value,title_pixels(input.frame,region.title)});
                cards->values.push_back(std::move(observation));
            }
        }
        if (desktop_) {
            cards->browser=locate_browser(input,regions); cards->browser->cards=cards->values;
            const auto progress=quest_progress_area(input);
            cards->browser->names_complete=cards->values.size()==regions.size() && quest_names_complete(input,regions)
                && (!regions.empty() || !progress);
            if (cards->values.empty() && !progress) { reading_area_.reset(); menu_.reset(); }
            else {
                double left=progress?progress->x:1,right=progress?progress->x+progress->width:0;
                for (const auto& card:regions) {
                    left=std::min(left,card.title.x); right=std::max(right,card.title.x+card.title.width);
                    for (const auto& row:card.rows) { left=std::min(left,row.area.x); right=std::max(right,row.area.x+row.area.width); }
                }
                Rect area{std::max(0.,left-.04),0,std::min(1.,right+.04)-std::max(0.,left-.04),1};
                if (!reading_area_ || area!=*reading_area_ || input.frame.captured-discovered_>=Time(5000)) discovered_=input.frame.captured;
                reading_area_=area; layout_context_={input.frame.window,input.generation,input.frame.captured};
                for (const auto& control:game::menu_controls(input)) if (control.tab==game::MenuTab::quests) menu_=control.bounds;
            }
        }
        if (desktop_ && cards->browser && !visible_.empty()) {
            scene_=cards->browser; width_=input.frame.width; height_=input.frame.height;
            layout_context_={input.frame.window,input.generation,input.frame.captured};
        }
        if (input.cancelled && input.cancelled()) return {};
        return cards->values.empty() && !desktop_?nullptr:cards;
    }
};
class DetectedSource final : public Source {
    std::shared_ptr<const game::DetectionSource> source_;
public:
    explicit DetectedSource(std::shared_ptr<const game::DetectionSource> source):source_(std::move(source)) {}
    std::optional<Snapshot> read() const override { return convert(source_->read()); }
    std::optional<Snapshot> peek() const override { return convert(source_->peek()); }
private:
    static std::optional<Snapshot> convert(const std::optional<game::DetectionResult>& result) {
        if (!result) return {};
        if (!result->error.empty()) throw std::runtime_error(result->error);
        const auto cards=std::dynamic_pointer_cast<const Cards>(result->value);
        if (!cards) throw std::runtime_error("Quest detector returned an incompatible result");
        return Snapshot{result->context,cards->values};
    }
};
class DetectedBrowser final : public BrowserSource {
    std::shared_ptr<const game::DetectionSource> source_;
public:
    explicit DetectedBrowser(std::shared_ptr<const game::DetectionSource> source):source_(std::move(source)) {}
    Observation<BrowserScene> read() const override {
        const auto result=source_->read(); if (!result) return {};
        if (!result->error.empty()) throw std::runtime_error(result->error);
        const auto cards=std::dynamic_pointer_cast<const Cards>(result->value);
        if (!cards || !cards->browser) throw std::runtime_error("Quest menu detector returned an incompatible result");
        const auto& c=result->context;
        return {Knowledge::present,*cards->browser,c.now,c.window,c.generation};
    }
};
}
Observation<Card> read_card(const game::DetectionInput& input,const CardRegion& region) {
    if (!input.frame.valid() || !input.frame.window.id || !input.generation || input.frame.captured<Time::zero() || input.text.size()>2048
        || !identifier(region.giver) || !identifier(region.quest) || region.rows.empty() || region.rows.size()>16)
        throw std::invalid_argument("Invalid quest card input");
    validate(region.title); std::set<std::string> ids; std::vector<Rect> areas{region.title};
    for (const auto& row : region.rows) {
        validate(row.area);
        if (!identifier(row.id) || !ids.insert(row.id).second || std::any_of(areas.begin(),areas.end(),[&](Rect area) { return overlaps(area,row.area); }))
            throw std::invalid_argument("Ambiguous quest objective regions");
        areas.push_back(row.area);
    }
    Observation<Card> result{Knowledge::unknown,{},input.frame.captured,input.frame.window,input.generation};
    const auto title=text(input,region.title);
    if (title.clipped || title.uncertain || normalized(title.value)!=normalized(region.quest)) return result;
    result.knowledge=Knowledge::present; result.value={region.giver,region.quest,{},false};
    const auto definition=resolve_quest(region.quest,region.giver);
    if (definition) result.value.quest=definition->name;
    const bool aligned=definition && region.rows.size()==definition->rows.size();
    result.value.complete_view=aligned;
    for (std::size_t index=0;index<region.rows.size();++index) {
        const auto& row=region.rows[index];
        const auto state=progress_state(input.frame,row.area);
        if (aligned && state!=ProgressColor::unknown)
            result.value.objectives.push_back(catalog_objective(*definition,index,row.id,state==ProgressColor::complete));
        else result.value.objectives.push_back({row.id,{}});
    }
    return result;
}
std::shared_ptr<const game::Detector> detector(std::shared_ptr<const LayoutSource> layout,std::vector<Field> fields) {
    if (!layout) throw std::invalid_argument("Quest detection needs card layout observations");
    Tracker validation(fields); return std::make_shared<QuestDetector>(std::move(layout));
}
std::shared_ptr<const Source> detection_source(std::shared_ptr<const game::DetectionSource> source) {
    if (!source) throw std::invalid_argument("Missing quest detection source");
    return std::make_shared<DetectedSource>(std::move(source));
}
std::shared_ptr<const game::Detector> desktop_detector(std::vector<Field> fields) {
    Tracker validation(fields); return std::make_shared<QuestDetector>(automatic_layout(),true);
}
std::shared_ptr<const BrowserSource> browser_source(std::shared_ptr<const game::DetectionSource> source) {
    if (!source) throw std::invalid_argument("Missing quest menu detection source");
    return std::make_shared<DetectedBrowser>(std::move(source));
}
}
