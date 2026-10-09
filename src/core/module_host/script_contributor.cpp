#include "providers.hpp"
#include "core/scripting/program.hpp"
#include "module.hpp"
#include "contributor.hpp"
#include <algorithm>

namespace hive::extensions {
namespace {
class ScriptContributor final : public Module {
    Manifest manifest_;
    Settings settings_;
    std::unique_ptr<automation::Contributor> contributor_;
    game::Requests batch_;
    std::uint64_t generation_{};
    Time now_{};
public:
    explicit ScriptContributor(Manifest manifest):manifest_(std::move(manifest)) {}
    void start(const Settings& settings) override {
        if (!manifest_.permissions.empty() || (manifest_.requests.empty() && manifest_.consumes.empty()))
            throw std::invalid_argument("Script contributors need request services and no input permissions");
        contributor_=scripting::script_contributor(manifest_.sources,settings); settings_=settings;
    }
    void update(const SessionView& view) override {
        now_=view.now;
        const auto generation=view.status.state==RunState::running?view.generation:0;
        if (!generation) { batch_={}; contributor_.reset(); generation_=0; return; }
        if (generation_!=generation) {
            contributor_=scripting::script_contributor(manifest_.sources,settings_); generation_=generation; batch_={};
        }
        auto batch=contributor_->requests({view.now,view.generation,view.feature,view.requests});
        if (batch.generation==generation_ && (batch_.generation!=generation_ || batch.revision>batch_.revision)) batch_=std::move(batch);
    }
    std::vector<Metric> metrics() const override {
        std::size_t active{};
        if (batch_.generation==generation_) for (const auto& [_,requests] : game::channel_batch(batch_).channels)
            active+=std::count_if(requests.begin(),requests.end(),[&](const auto& request) { return request.expires>now_; });
        return {{"Status",generation_?"Contributing requests":"Waiting for a session"},{"Active requests",std::to_string(active)}};
    }
    void stop() noexcept override { contributor_.reset(); batch_={}; generation_=0; }
    game::Requests requests() const override { return batch_; }
};
}
Factory script_contributor_factory() { return [](const Manifest& manifest) { return std::make_unique<ScriptContributor>(manifest); }; }
}
