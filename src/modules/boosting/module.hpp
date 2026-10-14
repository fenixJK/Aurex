#pragma once
#include "core/module_host/module.hpp"
#include "game/actions/inventory/hotbar.hpp"
#include "game/detection/field_boosts.hpp"
#include <array>
#include <mutex>

namespace hive::features::boosting {
enum class UseWhen { off, gathering, boosted, field_start, converting, backpack, boosted_conversion, fighting };
struct ItemRule { UseWhen when{}; Time interval{600000}; unsigned limit{1}; unsigned backpack{90}; std::string item; };
struct BoostItems {
    std::string field;
    unsigned dice_slot{},glitter_slot{},dice_limit{25},glitter_limit{1};
    Time roll_interval{3000},extend_after{780000};
    unsigned target_stacks{1};
    Time duration{};
    bool maintain{};
    std::string dice_item{"Field Dice"};
};
struct Preferences {
    bool follow{true},interrupt{true},during_boosts{},pre_glitter{};
    std::vector<std::string> fields,boosters;
    std::array<ItemRule,7> items;
    BoostItems boost;
};
void validate(const Preferences& preferences);
struct Booster { std::string name; automation::ProgramFactory visit; std::function<Time()> remaining; std::vector<std::string> fields; };
class Controller final : public automation::HotbarSource {
    mutable std::mutex mutex_;
    Preferences preferences_;
    std::array<Time,7> used_at_{};
    std::array<unsigned,7> uses_{};
    std::array<std::uint64_t,7> visits_{};
    std::uint64_t session_{},generation_{},visit_{};
    std::string gathering_field_,last_field_,active_,completed_;
    std::set<std::string> boosted_;
    std::map<std::string,std::optional<unsigned>> stacks_;
    std::optional<Time> boost_started_;
    struct BoostAge { std::optional<Time> started; bool extended{}; };
    std::map<std::string,BoostAge> ages_;
    std::optional<Time> last_dice_,last_glitter_;
    std::optional<Time> dice_hit_;
    std::string dice_field_;
    unsigned dice_used_{},glitter_used_{};
    Time observed_{},updated_{},next_item_{};
    std::optional<Time> blue_remaining_;
    bool enabled_{},running_{},readable_{};
    std::map<std::string,Time> attempted_;
    std::shared_ptr<const extensions::WorkSchedule> schedule_;
    std::string owner_;
    std::shared_ptr<game::ItemStock> stock_;
    std::string reserve_reason_;
    bool spend(const std::string& item,Time now);
    bool fresh(Time now) const;
    bool can_roll() const;
    unsigned take_boost(const automation::HotbarContext& context);
public:
    void supplies(std::shared_ptr<game::ItemStock> stock);
    void scheduling(std::shared_ptr<const extensions::WorkSchedule> schedule,std::string owner);
    std::optional<extensions::WorkWindow> protected_work(Time now) const;
    void configure(Preferences preferences);
    void stop();
    void update(const extensions::SessionView& view,const std::optional<game::DetectionResult>& reading,std::optional<Time> blue_remaining={});
    unsigned take(const automation::HotbarContext& context) override;
    bool needs_backpack() const override;
    game::Requests requests(Time now,std::uint64_t generation) const;
    std::optional<std::string> next_booster(Time now,const std::vector<Booster>& boosters,const std::set<std::string>& requested={}) const;
    void attempted(const std::string& name,Time now);
    std::vector<extensions::Metric> metrics() const;
    bool needs_reading() const;
};
extensions::Manifest module_manifest();
Settings migrate_settings(int version,Settings settings);
extensions::Factory module_factory(std::shared_ptr<Controller> controller,
    std::shared_ptr<const game::DetectionSource> observations,std::vector<Booster> boosters,std::vector<std::string> fields,
    std::shared_ptr<game::ItemStock> stock={},std::shared_ptr<const game::InventorySource> inventory={});
}
