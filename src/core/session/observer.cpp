#include "observer.hpp"
#include "recognition.hpp"
#include "game/detection/connection.hpp"
#include "game/detection/shift_lock.hpp"
#include "game/detection/menus.hpp"
#include "game/detection/landmarks.hpp"
#include "game/detection/respawn.hpp"
#include "game/detection/dialog.hpp"
#include <algorithm>
#include <set>

namespace hive::game {
namespace {
constexpr Time text_interval{750};
constexpr Time pixel_interval{50};
}
Observer::Observer(Desktop& desktop):desktop_(desktop) {
    try {
        capture_worker_=std::thread([this] { capture(); });
        pixel_worker_=std::thread([this] { detect(DetectionMode::pixels); });
        responsive_worker_=std::thread([this] { detect(DetectionMode::pixels,DetectionPriority::responsive); });
        text_worker_=std::thread([this] { detect(DetectionMode::text); });
    } catch (...) {
        { std::lock_guard lock(mutex_); exiting_=true; ++revision_; }
        wake_->changed.notify_all();
        if (capture_worker_.joinable()) capture_worker_.join();
        if (pixel_worker_.joinable()) pixel_worker_.join();
        if (responsive_worker_.joinable()) responsive_worker_.join();
        throw;
    }
}
Observer::~Observer() {
    { std::lock_guard lock(mutex_); exiting_=true; ++revision_; clear_detections(); }
    wake_->changed.notify_all(); capture_worker_.join(); pixel_worker_.join(); responsive_worker_.join(); text_worker_.join();
}
std::map<std::string,std::shared_ptr<const DetectionSource>> Observer::configure(std::vector<DetectorConfig> detectors) {
    std::lock_guard lock(mutex_);
    if (target_) throw std::logic_error("Stop observation before configuring detectors");
    std::set<std::string> ids; std::set<const Detector*> instances; std::vector<Entry> next;
    std::map<std::string,std::shared_ptr<const DetectionSource>> sources;
    for (auto& config : detectors) {
        if (config.id.empty() || config.id.size()>80 || std::any_of(config.id.begin(),config.id.end(),[](unsigned char c) { return c<32 || c==127; })
            || !ids.insert(config.id).second || !config.detector || !instances.insert(config.detector.get()).second
            || (config.mode!=DetectionMode::pixels && config.mode!=DetectionMode::text)
            || (config.priority!=DetectionPriority::normal && config.priority!=DetectionPriority::responsive)
            || (config.priority==DetectionPriority::responsive && config.mode!=DetectionMode::pixels)
            || config.interval<(config.mode==DetectionMode::pixels?pixel_interval:text_interval) || config.interval>Time(60000))
            throw std::invalid_argument("Invalid detector configuration");
        auto channel=std::make_shared<DetectionChannel>(); channel->wake_=wake_; channel->on_demand_=config.on_demand; sources.emplace(config.id,channel);
        next.push_back({std::move(config),std::move(channel),{}});
    }
    ++revision_; clear_detections(); detectors_=std::move(next); wake_->changed.notify_all(); return sources;
}
bool Observer::has_detectors() const { std::lock_guard lock(mutex_); return !detectors_.empty(); }
std::shared_ptr<const Frame> Observer::latest_frame() const { std::lock_guard lock(mutex_); return frame_; }
void Observer::configure_speed(std::shared_ptr<const MovementSpeedSource> source) {
    std::lock_guard lock(mutex_);
    if (target_) throw std::logic_error("Stop observation before changing movement speed");
    speed_source_=std::move(source);
    if (speed_source_) speed_source_->reset();
}
void Observer::clear_detections() {
    frame_.reset(); frame_hud_.reset(); capture_error_.clear(); ocr_next_={};
    speed_observation_={}; connection_regions_.clear();
    if (speed_source_) speed_source_->reset();
    for (auto& entry : detectors_) { entry.channel->clear(); entry.next={}; }
}
void Observer::watch(const ObservationContext& context,bool hud,bool connection) {
    hud=hud || connection;
    if (!context.window.id || !context.window.bounds.valid() || !context.generation || context.now<Time::zero()) throw std::invalid_argument("Invalid observation target");
    {
        std::lock_guard lock(mutex_);
        const bool same=target_ && target_->generation==context.generation && target_->window.id==context.window.id
            && target_->window.process==context.window.process && target_->window.bounds==context.window.bounds;
        if (same) {
            if (hud_==hud && connection_==connection) return;
            hud_=hud; connection_=connection; ++revision_;
        }
        else { target_=context; hud_=hud; connection_=connection; latest_={}; ++revision_; clear_detections(); }
    }
    wake_->changed.notify_all();
}
void Observer::stop() {
    { std::lock_guard lock(mutex_); target_.reset(); latest_={}; text_regions_.clear(); ++revision_; clear_detections(); }
    wake_->changed.notify_all();
}
void Observer::request_text(std::vector<Rect> regions) {
    if (regions.size()>64 || std::any_of(regions.begin(),regions.end(),[](const auto& r) {
        return !r.valid() || r.x<0 || r.y<0 || r.x+r.width>1 || r.y+r.height>1;
    })) throw std::invalid_argument("Invalid requested text regions");
    {
        std::lock_guard lock(mutex_);
        if (text_regions_==regions) return;
        text_regions_=std::move(regions); ++text_revision_; ++wake_->revision;
    }
    wake_->changed.notify_all();
}
ObservedState Observer::latest(bool request_speed) {
    std::lock_guard lock(mutex_); auto result=latest_;
    if (request_speed && target_ && speed_source_) {
        auto context=*target_; context.now=monotonic_now();
        try { result.state.movement_speed=speed_source_->read(context,result.state.movement_speed); }
        catch (...) { result.state.movement_speed={}; result.error="Could not read movement speed"; }
        speed_observation_=result.state.movement_speed;
    } else if (speed_source_) result.state.movement_speed=speed_observation_;
    if (!capture_error_.empty()) result.error=capture_error_;
    return result;
}
void Observer::capture() {
    HudInspector hud_inspector;
    std::unique_lock lock(mutex_);
    while (!exiting_) {
        wake_->changed.wait(lock,[this] { return exiting_ || target_.has_value(); });
        if (exiting_) break;
        const auto context=*target_; const auto revision=revision_.load(); const bool hud=hud_,connection=connection_;
        const auto demand=wake_->revision.load();
        auto interval=hud_ || !text_regions_.empty()?text_interval:Time::max();
        for (const auto& entry : detectors_) if (!entry.config.on_demand || entry.channel->wanted(monotonic_now())) interval=std::min(interval,entry.config.interval);
        if (interval==Time::max()) {
            frame_.reset(); frame_hud_.reset();
            wake_->changed.wait_for(lock,Time(250),[this,revision,demand] { return exiting_ || revision!=revision_ || demand!=wake_->revision; }); continue;
        }
        lock.unlock(); const auto began=monotonic_now();
        std::shared_ptr<const Frame> result; std::string error; Observation<bool> shift_lock,game_hud,alive; Observation<Point> open_menu;
        std::shared_ptr<const HudFeatures> features;
        Observation<std::vector<MenuControl>> menus;
        Observation<DialogButtons> confirmation;
        Observation<double> backpack;
        std::vector<Rect> connection_regions;
        try {
            auto frame=desktop_.capture(context.window.id);
            if (!frame.valid() || frame.window.id!=context.window.id || frame.window.process!=context.window.process
                || frame.window.bounds!=context.window.bounds || frame.captured<context.now || frame.captured>monotonic_now())
                throw std::runtime_error("Game window changed during capture or returned an invalid timestamp");
            if (hud) {
                confirmation=read_confirmation(frame,context.generation);
                features=std::make_shared<const HudFeatures>(hud_inspector.inspect(frame,context.generation));
                backpack=features->backpack;
                const auto& controls=features->menus;
                menus={Knowledge::present,controls,frame.captured,frame.window,context.generation};
                game_hud={Knowledge::present,game_hud_visible(*features),frame.captured,frame.window,context.generation};
                if (connection) connection_regions=connection_text_regions(frame,game_hud.value);
                if (game_hud.value) {
                    const auto health=read_empty_health_bar(frame,context.generation);
                    if (health.knowledge==Knowledge::present) {
                        alive=health; alive.value=!health.value;
                    }
                }
                if (controls.size()>=2) shift_lock=read_shift_lock(frame,context.generation);
                for (const auto& control:controls) if (control.selected==true)
                    open_menu={Knowledge::present,control.center(),frame.captured,frame.window,context.generation};
                if (open_menu.knowledge==Knowledge::unknown && controls.size()==3
                    && std::all_of(controls.begin(),controls.end(),[](const auto& control) { return control.selected==false; }))
                    open_menu={Knowledge::absent,{},frame.captured,frame.window,context.generation};
            }
            result=std::make_shared<const Frame>(std::move(frame));
        } catch (const std::exception& e) { error=std::string(e.what()).substr(0,4096); if (error.empty()) error="Could not capture game window"; }
        catch (...) { error="Could not capture game window"; }
        lock.lock();
        if (revision==revision_ && !exiting_) {
            if (result && frame_ && result->captured<=frame_->captured) {
                result.reset(); error="Capture timestamp did not advance";
            }
            capture_error_=std::move(error);
            if (result) {
                if (hud) { latest_.state.backpack_percent=backpack; latest_.state.alive=alive; latest_.state.game_hud=game_hud; latest_.state.shift_lock=shift_lock; latest_.state.open_menu=open_menu; latest_.state.menus=std::move(menus); latest_.state.confirmation=confirmation; }
                if (connection_regions_!=connection_regions) {
                    connection_regions_=std::move(connection_regions); ++wake_->revision;
                }
                if (hud && game_hud.value && connection_regions_.empty())
                    latest_.state.connection={Knowledge::present,Connection::connected,game_hud.captured,game_hud.window,game_hud.generation};
                if (alive.knowledge==Knowledge::present && !alive.value) latest_.state.last_death={Knowledge::present,true,alive.captured,alive.window,alive.generation};
                frame_=std::move(result); frame_hud_=std::move(features); ++frame_sequence_; wake_->changed.notify_all();
            }
        }
        const auto next=std::max(began+interval,monotonic_now()+Time(10));
        wake_->changed.wait_until(lock,std::chrono::steady_clock::time_point(next),[this,revision,demand] { return exiting_ || revision!=revision_ || demand!=wake_->revision; });
    }
}
void Observer::detect(DetectionMode mode,DetectionPriority priority) {
    std::unique_lock lock(mutex_); std::uint64_t seen{},watch_revision{};
    RecognitionCache recognition,prompt_recognition;
    while (!exiting_) {
        if (!target_) { recognition.clear(); prompt_recognition.clear(); }
        wake_->changed.wait(lock,[this] { return exiting_ || target_.has_value(); });
        if (exiting_) break;
        const auto revision=revision_.load();
        const auto text_revision=text_revision_.load();
        const auto demand=wake_->revision.load();
        if (watch_revision!=revision) { seen=0; watch_revision=revision; recognition.clear(); prompt_recognition.clear(); }
        auto due=mode==DetectionMode::text && (!text_regions_.empty() || !connection_regions_.empty())?Time{}:Time::max();
        for (const auto& entry : detectors_) if (entry.config.mode==mode && entry.config.priority==priority && (!entry.config.on_demand || entry.channel->wanted(monotonic_now()))) due=std::min(due,entry.next);
        if (due==Time::max()) {
            recognition.clear(); prompt_recognition.clear();
            wake_->changed.wait(lock,[this,revision,text_revision,demand] { return exiting_ || revision!=revision_ || text_revision!=text_revision_ || demand!=wake_->revision; }); continue;
        }
        if (mode==DetectionMode::text) due=std::max(due,ocr_next_);
        if (monotonic_now()<due) {
            wake_->changed.wait_until(lock,std::chrono::steady_clock::time_point(due)); continue;
        }
        if (!frame_ || seen==frame_sequence_) {
            wake_->changed.wait(lock,[this,revision,text_revision,seen] { return exiting_ || revision!=revision_ || text_revision!=text_revision_ || (frame_ && frame_sequence_!=seen); }); continue;
        }
        const auto frame=frame_; seen=frame_sequence_;
        const auto features=frame_hud_;
        const auto generation=target_->generation; const auto began=monotonic_now();
        std::vector<Rect> regions;
        if (mode==DetectionMode::text) regions=text_regions_;
        if (mode==DetectionMode::text) regions.insert(regions.end(),connection_regions_.begin(),connection_regions_.end());
        std::vector<Entry> entries;
        for (auto& entry : detectors_) if (entry.config.mode==mode && entry.config.priority==priority && entry.next<=began && (!entry.config.on_demand || entry.channel->wanted(began))) {
            entries.push_back(entry); entry.next=began+entry.config.interval;
        }
        if (mode==DetectionMode::pixels) std::stable_sort(entries.begin(),entries.end(),[](const auto& a,const auto& b) { return a.config.interval<b.config.interval; });
        if (mode==DetectionMode::text) ocr_next_=began+text_interval;
        lock.unlock();
        auto cancelled=[this,revision,text_revision,mode] { return revision_.load()!=revision || (mode==DetectionMode::text && text_revision_.load()!=text_revision); };
        std::vector<TextLine> lines; bool readable=true;
        if (mode==DetectionMode::text) {
            ObservedState result;
            bool requested=false,hive_requested=false;
            try {
                for (auto entry=entries.begin();entry!=entries.end();) {
                    std::unique_lock invocation(entry->config.detector->invocation_,std::try_to_lock);
                    if (!invocation.owns_lock()) { entry=entries.erase(entry); continue; }
                    const auto requested=entry->config.detector->text_regions({*frame,{},generation,cancelled,{},features.get()});
                    ++entry;
                    regions.insert(regions.end(),requested.begin(),requested.end());
                    if (regions.size()>64 || std::find(regions.begin(),regions.end(),Rect{0,0,1,1})!=regions.end()) {
                        regions={{0,0,1,1}}; break;
                    }
                }
                if (regions.size()>64) regions={{0,0,1,1}};
                requested=!regions.empty();
                auto prompt=requested?interaction_text_region(*frame,features.get()):std::optional<Rect>{};
                if (prompt && std::none_of(regions.begin(),regions.end(),[&](const auto& r) {
                    return r.x<prompt->x+prompt->width && r.x+r.width>prompt->x
                        && r.y<prompt->y+prompt->height && r.y+r.height>prompt->y;
                })) prompt.reset();
                const auto hive_region=interaction_prompt_search_region;
                hive_requested=prompt.has_value() || std::any_of(regions.begin(),regions.end(),[&](const auto& r) {
                    return r.x<=hive_region.x && r.y<=hive_region.y && r.x+r.width>=hive_region.x+hive_region.width && r.y+r.height>=hive_region.y+hive_region.height;
                });
                std::erase(regions,interaction_prompt_search_region);
                if (prompt) {
                    regions=exclude_text_region(std::move(regions),*prompt);
                }
                if (regions.size()>64) regions=prompt?exclude_text_region({{0,0,1,1}},*prompt):std::vector<Rect>{{0,0,1,1}};
                lines=recognition.read(desktop_,*frame,generation,std::move(regions),cancelled);
                if (prompt) {
                    const auto image=interaction_text_image(*frame,*prompt);
                    std::vector<TextLine> isolated;
                    if (const auto label=interaction_label(image)) isolated.push_back(*label);
                    else isolated=prompt_recognition.read(desktop_,image,generation,{{0,0,1,1}},cancelled);
                    for (auto& line:isolated) {
                        line.pixels.x+=std::floor(prompt->x*frame->width)-interaction_text_padding;
                        line.pixels.y+=std::floor(prompt->y*frame->height)-interaction_text_padding;
                    }
                    lines.insert(lines.end(),isolated.begin(),isolated.end());
                } else prompt_recognition.clear();
                if (requested) result.state=read_game_text(*frame,lines,generation);
            } catch (const std::exception& e) { result.error=e.what(); readable=false; }
            catch (...) { result.error="Could not read game window"; readable=false; }
            result.processing=monotonic_now()-began;
            lock.lock();
            if (!cancelled() && !exiting_ && requested) {
                latest_.state.text=std::move(result.state.text);
                if (result.state.connection.knowledge!=Knowledge::unknown && result.state.connection.captured>=latest_.state.connection.captured)
                    latest_.state.connection=result.state.connection;
                if (hive_requested || result.state.hive_prompt.knowledge==Knowledge::present) {
                    latest_.state.hive_prompt=result.state.hive_prompt;
                    latest_.state.at_hive=result.state.at_hive;
                }
                latest_.error=std::move(result.error); latest_.processing=result.processing;
            }
            lock.unlock();
        }
        for (const auto& entry : entries) {
            if (!readable || cancelled()) break;
            std::unique_lock invocation(entry.config.detector->invocation_,std::try_to_lock);
            if (!invocation.owns_lock() || cancelled()) continue;
            DetectionResult reading{{frame->window,generation,frame->captured},{},{}};
            try {
                auto recognize=[&](const Frame& crop) {
                    if (mode!=DetectionMode::text) throw std::logic_error("Pixel detection cannot request OCR");
                    return desktop_.recognize(crop,cancelled);
                };
                reading.value=entry.config.detector->detect({*frame,lines,generation,cancelled,recognize,features.get()});
            }
            catch (const std::exception& e) { reading.error=std::string(e.what()).substr(0,4096); if (reading.error.empty()) reading.error="Detector failed"; }
            catch (...) { reading.error="Detector failed"; }
            lock.lock();
            if (!cancelled() && !exiting_) entry.channel->publish(std::move(reading));
            lock.unlock();
        }
        lock.lock();
    }
}
}
