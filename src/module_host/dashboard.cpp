#include "hive/module_host/providers.hpp"
#include "hive/module_host/module.hpp"

namespace hive::extensions {
namespace {
class Dashboard final : public Module {
    std::vector<Binding> bindings_;
    std::vector<Metric> metrics_;
public:
    explicit Dashboard(const Manifest& manifest): bindings_(manifest.bindings) {
        if (!manifest.settings.empty() || bindings_.empty()) throw std::runtime_error("Dashboard packages require bindings and no settings");
    }
    void start(const Settings&) override { metrics_.clear(); }
    void update(const SessionView& view) override {
        metrics_.clear();
        for (const auto& binding : bindings_) {
            std::string value;
            const auto& s=view.status;
            if (binding.signal=="session.state") value=run_state_name(s.state);
            else if (binding.signal=="session.job") value=s.job;
            else if (binding.signal=="session.action") value=s.action;
            else if (binding.signal=="session.reason") value=s.reason;
            else if (binding.signal=="session.completed-actions") value=std::to_string(s.completed_actions);
            else if (binding.signal=="session.queued-jobs") value=std::to_string(s.queued_jobs);
            else throw std::runtime_error("Unsupported dashboard signal");
            metrics_.push_back({binding.label,value.empty()?"—":value});
        }
    }
    std::vector<Metric> metrics() const override { return metrics_; }
    void stop() noexcept override { metrics_.clear(); }
};
}
Factory dashboard_factory() { return [](const Manifest& manifest) { return std::make_unique<Dashboard>(manifest); }; }
}
