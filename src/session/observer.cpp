#include "hive/session/observer.hpp"
#include "hive/session/recognition.hpp"
#include "hive/game/detection/shift_lock.hpp"
#include "hive/game/detection/menus.hpp"
#include "hive/game/detection/landmarks.hpp"
#include "hive/game/detection/respawn.hpp"
#include "hive/game/detection/dialog.hpp"
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
        text_worker_=std::thread([this] { detect(DetectionMode::text); });
    } catch (...) {
        { std::lock_guard lock(mutex_); exiting_=true; ++revision_; }
        wake_->changed.notify_all();
        if (capture_worker_.joinable()) capture_worker_.join();
        if (pixel_worker_.joinable()) pixel_worker_.join();
        throw;
    }
}
Observer::~Observer() {
    { std::lock_guard lock(mutex_); exiting_=true; ++revision_; clear_detections(); }
    wake_->changed.notify_all(); capture_worker_.join(); pixel_worker_.join(); text_worker_.join();
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
    frame_.reset(); capture_error_.clear(); hud_next_=ocr_next_={};
    speed_observation_={}; honey_.reset();
    if (speed_source_) speed_source_->reset();
    for (auto& entry : detectors_) { entry.channel->clear(); entry.next={}; }
}
void Observer::watch(const ObservationContext& context,bool hud) {
    if (!context.window.id || !context.window.bounds.valid() || !context.generation || context.now<Time::zero()) throw std::invalid_argument("Invalid observation target");
    {
        std::lock_guard lock(mutex_);
        const bool same=target_ && target_->generation==context.generation && target_->window.id==context.window.id
            && target_->window.process==context.window.process && target_->window.bounds==context.window.bounds;
        if (same) { if (hud_!=hud) { hud_=hud; ++revision_; } }
        else { target_=context; hud_=hud; latest_={}; ++revision_; clear_detections(); }
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
        text_regions_=std::move(regions); ++revision_; hud_next_=ocr_next_={};
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
    std::unique_lock lock(mutex_);
    while (!exiting_) {
        wake_->changed.wait(lock,[this] { return exiting_ || target_.has_value(); });
        if (exiting_) break;
        const auto context=*target_; const auto revision=revision_.load(); const bool hud=hud_;
        const auto demand=wake_->revision.load();
        auto interval=hud_?text_interval:Time::max();
        for (const auto& entry : detectors_) if (!entry.config.on_demand || entry.channel->wanted(monotonic_now())) interval=std::min(interval,entry.config.interval);
        if (interval==Time::max()) {
            frame_.reset();
            wake_->changed.wait_for(lock,Time(250),[this,revision,demand] { return exiting_ || revision!=revision_ || demand!=wake_->revision; }); continue;
        }
        lock.unlock(); const auto began=monotonic_now();
        std::shared_ptr<const Frame> result; std::string error; Observation<bool> shift_lock,game_hud,alive; Observation<Point> open_menu;
        Observation<std::vector<MenuControl>> menus;
        Observation<DialogButtons> confirmation;
        Observation<double> backpack;
        try {
            auto frame=desktop_.capture(context.window.id);
            if (!frame.valid() || frame.window.id!=context.window.id || frame.window.process!=context.window.process
                || frame.window.bounds!=context.window.bounds || frame.captured<context.now || frame.captured>monotonic_now())
                throw std::runtime_error("Game window changed during capture or returned an invalid timestamp");
            if (hud) {
                confirmation=read_confirmation(frame,context.generation);
                backpack=read_backpack_percent(frame,context.generation);
                const auto controls=menu_controls({frame,{},context.generation,{}});
                menus={Knowledge::present,controls,frame.captured,frame.window,context.generation};
                game_hud={Knowledge::present,backpack.knowledge==Knowledge::present || std::count_if(controls.begin(),controls.end(),
                    [](const auto& control) { return control.selected.has_value(); })>=2,frame.captured,frame.window,context.generation};
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
                frame_=std::move(result); ++frame_sequence_; wake_->changed.notify_all();
            }
        }
        const auto next=std::max(began+interval,monotonic_now()+Time(10));
        wake_->changed.wait_until(lock,std::chrono::steady_clock::time_point(next),[this,revision,demand] { return exiting_ || revision!=revision_ || demand!=wake_->revision; });
    }
}
void Observer::detect(DetectionMode mode) {
    std::unique_lock lock(mutex_); std::uint64_t seen{},watch_revision{};
    RecognitionCache recognition,prompt_recognition;
    while (!exiting_) {
        if (!target_) { recognition.clear(); prompt_recognition.clear(); }
        wake_->changed.wait(lock,[this] { return exiting_ || target_.has_value(); });
        if (exiting_) break;
        const auto revision=revision_.load();
        const auto demand=wake_->revision.load();
        if (watch_revision!=revision) { seen=0; watch_revision=revision; recognition.clear(); prompt_recognition.clear(); }
        auto due=mode==DetectionMode::text && hud_?hud_next_:Time::max();
        for (const auto& entry : detectors_) if (entry.config.mode==mode && (!entry.config.on_demand || entry.channel->wanted(monotonic_now()))) due=std::min(due,entry.next);
        if (due==Time::max()) {
            recognition.clear(); prompt_recognition.clear();
            wake_->changed.wait_for(lock,Time(250),[this,revision,demand] { return exiting_ || revision!=revision_ || demand!=wake_->revision; }); continue;
        }
        if (mode==DetectionMode::text) due=std::max(due,ocr_next_);
        if (monotonic_now()<due) {
            wake_->changed.wait_until(lock,std::chrono::steady_clock::time_point(due)); continue;
        }
        if (!frame_ || seen==frame_sequence_) {
            wake_->changed.wait(lock,[this,revision,seen] { return exiting_ || revision!=revision_ || (frame_ && frame_sequence_!=seen); }); continue;
        }
        const auto frame=frame_; seen=frame_sequence_;
        const auto generation=target_->generation; const auto began=monotonic_now(); const bool hud=hud_;
        std::vector<Rect> regions;
        if (mode==DetectionMode::text) regions=text_regions_;
        if (mode==DetectionMode::text && hud) {
            const auto& known=latest_.state.game_hud;
            const auto requested=hud_text_regions(known.has_value({frame->window,generation,frame->captured},Time(2000)) && known.value);
            regions.insert(regions.end(),requested.begin(),requested.end());
        }
        std::vector<Entry> entries;
        for (auto& entry : detectors_) if (entry.config.mode==mode && entry.next<=began && (!entry.config.on_demand || entry.channel->wanted(began))) {
            entries.push_back(entry); entry.next=began+entry.config.interval;
        }
        if (mode==DetectionMode::text) { ocr_next_=began+text_interval; if (hud_) hud_next_=ocr_next_; }
        lock.unlock();
        auto cancelled=[this,revision] { return revision_.load()!=revision; };
        std::vector<TextLine> lines; bool readable=true;
        if (mode==DetectionMode::text) {
            ObservedState result;
            try {
                for (const auto& entry:entries) {
                    std::unique_lock invocation(entry.config.detector->invocation_,std::try_to_lock);
                    if (!invocation.owns_lock()) { regions={{0,0,1,1}}; break; }
                    const auto requested=entry.config.detector->text_regions(*frame,generation);
                    regions.insert(regions.end(),requested.begin(),requested.end());
                    if (regions.size()>64 || std::find(regions.begin(),regions.end(),Rect{0,0,1,1})!=regions.end()) {
                        regions={{0,0,1,1}}; break;
                    }
                }
                if (regions.size()>64) regions={{0,0,1,1}};
                lines=recognition.read(desktop_,*frame,generation,std::move(regions),cancelled);
                if (const auto prompt=hud?interaction_text_region(*frame):std::optional<Rect>{}) {
                    const auto image=interaction_text_image(*frame,*prompt);
                    auto isolated=prompt_recognition.read(desktop_,image,generation,{{0,0,1,1}},cancelled);
                    for (auto& line:isolated) {
                        line.pixels.x+=std::floor(prompt->x*frame->width); line.pixels.y+=std::floor(prompt->y*frame->height);
                    }
                    if (!isolated.empty()) {
                        std::erase_if(lines,[&](const auto& line) {
                            const double x=(line.pixels.x+line.pixels.width/2)/frame->width;
                            const double y=(line.pixels.y+line.pixels.height/2)/frame->height;
                            return x>=prompt->x && x<=prompt->x+prompt->width && y>=prompt->y && y<=prompt->y+prompt->height;
                        });
                        lines.insert(lines.end(),isolated.begin(),isolated.end());
                    }
                } else prompt_recognition.clear();
                result.state=read_hud(*frame,lines,generation);
                if (hud && result.state.text.knowledge!=Knowledge::present) result.error="Game text is not readable";
            } catch (const std::exception& e) { result.error=e.what(); readable=false; }
            catch (...) { result.error="Could not read game window"; readable=false; }
            result.processing=monotonic_now()-began;
            lock.lock();
            if (!cancelled() && !exiting_) {
                result.state.honey=honey_.observe({frame->window,generation,frame->captured},result.state.honey);
                result.state.alive=latest_.state.alive;
                result.state.backpack_percent=latest_.state.backpack_percent;
                if (result.state.text.knowledge==Knowledge::unknown) result.state.text=latest_.state.text;
                if (latest_.state.shift_lock.captured>result.state.shift_lock.captured) result.state.shift_lock=latest_.state.shift_lock;
                result.state.open_menu=latest_.state.open_menu;
                result.state.menus=latest_.state.menus;
                result.state.game_hud=latest_.state.game_hud;
                result.state.confirmation=latest_.state.confirmation;
                latest_=std::move(result);
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
                reading.value=entry.config.detector->detect({*frame,lines,generation,cancelled,recognize});
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
