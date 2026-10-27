#include "observer.hpp"
#include "recognition.hpp"
#include "game/detection/state_observation.hpp"
#include "game/detection/landmarks.hpp"
#include <algorithm>
#include <set>

namespace hive::game {
namespace {
constexpr Time text_interval{750};
constexpr Time pixel_interval{50};
bool covers(Rect captured,Rect required) {
    return captured.x<=required.x && captured.y<=required.y
        && captured.x+captured.width+1e-6>=required.x+required.width
        && captured.y+captured.height+1e-6>=required.y+required.height;
}
}
Observer::Observer(Desktop& desktop):desktop_(desktop) {
    configure({});
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
    for (auto& sensor:state_sensors()) {
        ids.insert(sensor.config.id);
        next.push_back({std::move(sensor.config),std::make_shared<DetectionChannel>(),{},sensor.kind});
    }
    for (auto& config : detectors) {
        const auto region=config.detector?config.detector->capture_region():Rect{};
        if (!region.valid() || region.x<0 || region.y<0 || region.x+region.width>1 || region.y+region.height>1)
            throw std::invalid_argument("Invalid detector capture region");
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
bool Observer::has_detectors() const { std::lock_guard lock(mutex_); return std::any_of(detectors_.begin(),detectors_.end(),[](const auto& entry) { return !entry.observation; }); }
bool Observer::wanted(const Entry& entry,Time now) const {
    if (entry.observation) return needs_.contains(*entry.observation);
    return !entry.config.on_demand || entry.channel->wanted(now);
}
std::shared_ptr<const Frame> Observer::latest_frame() const { std::lock_guard lock(mutex_); return frame_; }
void Observer::configure_speed(std::shared_ptr<const MovementSpeedSource> source) {
    std::lock_guard lock(mutex_);
    if (target_) throw std::logic_error("Stop observation before changing movement speed");
    speed_source_=std::move(source);
    if (speed_source_) speed_source_->reset();
}
void Observer::clear_detections() {
    frame_.reset(); responsive_frame_.reset(); capture_error_.clear(); labels_error_.clear(); ocr_next_={}; labels_next_={};
    speed_observation_={};
    if (speed_source_) speed_source_->reset();
    for (auto& entry : detectors_) { entry.channel->clear(); entry.next={}; entry.captured=Time(-1); }
}
void Observer::watch(const ObservationContext& context,ObservationNeeds needs) {
    if (!context.window.id || !context.window.bounds.valid() || !context.generation || context.now<Time::zero()) throw std::invalid_argument("Invalid observation target");
    {
        std::lock_guard lock(mutex_);
        const bool same=target_ && target_->generation==context.generation && target_->window.id==context.window.id
            && target_->window.process==context.window.process && target_->window.bounds==context.window.bounds;
        if (same) {
            if (needs_==needs) return;
            for (auto& entry:detectors_) if (entry.observation && needs.contains(*entry.observation) && !needs_.contains(*entry.observation)) entry.next={};
            needs_=std::move(needs); ++wake_->revision;
        }
        else { target_=context; needs_=std::move(needs); latest_={}; ++revision_; clear_detections(); }
    }
    wake_->changed.notify_all();
}
void Observer::stop() {
    { std::lock_guard lock(mutex_); target_.reset(); needs_.clear(); latest_={}; text_regions_.clear(); label_queries_.clear(); ++label_revision_; ++revision_; clear_detections(); }
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
void Observer::request_labels(std::vector<FixedLabelQuery> queries) {
    if (queries.size()>16 || std::any_of(queries.begin(),queries.end(),[](const auto& query) {
        const auto r=query.region;
        return !r.valid() || (query.anchor.empty() && (r.x<0 || r.y<0 || r.x+r.width>1 || r.y+r.height>1))
            || query.labels.empty() || query.labels.size()>128;
    })) throw std::invalid_argument("Invalid fixed UI label request");
    {
        std::lock_guard lock(mutex_);
        if (label_queries_==queries) return;
        label_queries_=std::move(queries); labels_next_={}; labels_error_.clear(); latest_.state.fixed_labels={}; ++label_revision_; ++wake_->revision;
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
    for (const auto& entry:detectors_) if (entry.observation && wanted(entry,monotonic_now()))
        if (const auto reading=entry.channel->peek(); reading && !reading->error.empty()) result.error=reading->error;
    if (!labels_error_.empty()) result.error=labels_error_;
    if (!capture_error_.empty()) result.error=capture_error_;
    result.state.observation_error=result.error;
    return result;
}
void Observer::capture() {
    Time last_full{},last_responsive{},retry_after{};
    std::uint64_t schedule_revision{};
    std::unique_lock lock(mutex_);
    while (!exiting_) {
        wake_->changed.wait(lock,[this] { return exiting_ || target_.has_value(); });
        if (exiting_) break;
        const auto context=*target_; const auto revision=revision_.load();
        if (schedule_revision!=revision) {
            last_full=last_responsive=retry_after=Time{}; schedule_revision=revision;
        }
        const auto demand=wake_->revision.load(); const auto now=monotonic_now();
        auto full_interval=!text_regions_.empty()?text_interval:Time::max();
        auto regional_interval=Time::max();
        std::optional<Rect> regional;
        bool responsive=false;
        const Rect entire{0,0,1,1};
        for (const auto& entry:detectors_) if (wanted(entry,now)) {
            const auto region=entry.config.detector->capture_region();
            if (entry.config.priority==DetectionPriority::responsive) {
                responsive=true;
                if (region!=entire) {
                    regional_interval=std::min(regional_interval,entry.config.interval);
                    if (!regional) regional=region;
                    else {
                        const auto prior=*regional;
                        const double left=std::min(prior.x,region.x),top=std::min(prior.y,region.y);
                        regional=Rect{left,top,std::max(prior.x+prior.width,region.x+region.width)-left,
                            std::max(prior.y+prior.height,region.y+region.height)-top};
                    }
                    continue;
                }
            }
            full_interval=std::min(full_interval,entry.config.interval);
        }
        if (!label_queries_.empty()) full_interval=std::min(full_interval,Time(100));
        const auto next_full=full_interval==Time::max()?Time::max():last_full+full_interval;
        const auto next_regional=regional_interval==Time::max()?Time::max():last_responsive+regional_interval;
        auto due=std::min(next_full,next_regional);
        if (due==Time::max()) {
            wake_->changed.wait_for(lock,Time(250),[this,revision,demand] { return exiting_ || revision!=revision_ || demand!=wake_->revision; }); continue;
        }
        due=std::max(due,retry_after);
        if (now<due) {
            wake_->changed.wait_until(lock,std::chrono::steady_clock::time_point(due),[this,revision,demand] { return exiting_ || revision!=revision_ || demand!=wake_->revision; }); continue;
        }
        const bool full=now>=next_full;
        const auto region=full?entire:*regional;
        lock.unlock(); const auto began=monotonic_now();
        std::shared_ptr<const Frame> result; std::string error;
        try {
            auto frame=full?desktop_.capture(context.window.id):desktop_.capture(context.window.id,region);
            if (!frame.valid() || frame.window.id!=context.window.id || frame.window.process!=context.window.process
                || frame.window.bounds!=context.window.bounds || frame.captured<context.now || frame.captured>monotonic_now()
                || !covers(frame.region,region))
                throw std::runtime_error("Game window changed during capture or returned an invalid region/timestamp");
            result=std::make_shared<const Frame>(std::move(frame));
        } catch (const std::exception& e) { error=std::string(e.what()).substr(0,4096); if (error.empty()) error="Could not capture game window"; }
        catch (...) { error="Could not capture game window"; }
        lock.lock();
        if (revision==revision_ && !exiting_) {
            const auto previous=responsive?responsive_frame_:frame_;
            if (result && previous && result->captured<=previous->captured) { result.reset(); error="Capture timestamp did not advance"; }
            capture_error_=std::move(error);
            if (result) {
                if (full) { frame_=result; ++frame_sequence_; }
                if (responsive) { responsive_frame_=std::move(result); ++responsive_sequence_; }
                wake_->changed.notify_all();
            }
        }
        retry_after=monotonic_now()+Time(10);
        if (full) last_full=began;
        if (responsive) last_responsive=began;
    }
}
void Observer::detect(DetectionMode mode,DetectionPriority priority) {
    const bool responsive=priority==DetectionPriority::responsive;
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
        auto due=mode==DetectionMode::text && !text_regions_.empty()?Time{}:Time::max();
        const bool reads_labels=mode==DetectionMode::pixels && priority==DetectionPriority::normal;
        if (reads_labels && !label_queries_.empty()) due=std::min(due,labels_next_);
        for (const auto& entry : detectors_) if (entry.config.mode==mode && entry.config.priority==priority && wanted(entry,monotonic_now())) due=std::min(due,entry.next);
        if (due==Time::max()) {
            recognition.clear(); prompt_recognition.clear();
            wake_->changed.wait(lock,[this,revision,text_revision,demand] { return exiting_ || revision!=revision_ || text_revision!=text_revision_ || demand!=wake_->revision; }); continue;
        }
        if (mode==DetectionMode::text) due=std::max(due,ocr_next_);
        if (monotonic_now()<due) {
            wake_->changed.wait_until(lock,std::chrono::steady_clock::time_point(due)); continue;
        }
        const auto frame=responsive?responsive_frame_:frame_;
        const auto sequence=responsive?responsive_sequence_:frame_sequence_;
        if (!frame || seen==sequence) {
            wake_->changed.wait(lock,[this,revision,text_revision,seen,responsive] {
                return exiting_ || revision!=revision_ || text_revision!=text_revision_
                    || (responsive?(responsive_frame_ && responsive_sequence_!=seen):(frame_ && frame_sequence_!=seen));
            }); continue;
        }
        seen=sequence;
        const auto generation=target_->generation; const auto began=monotonic_now();
        std::vector<Rect> regions;
        if (mode==DetectionMode::text) regions=text_regions_;
        struct Invocation { Entry entry; std::shared_ptr<const Frame> frame; };
        std::vector<Invocation> entries;
        for (auto& entry : detectors_) if (entry.config.mode==mode && entry.config.priority==priority && entry.next<=began && wanted(entry,began)) {
            auto image=frame;
            if (!covers(image->region,entry.config.detector->capture_region())) image=frame_;
            if (!image || image->captured<=entry.captured || !covers(image->region,entry.config.detector->capture_region())) continue;
            entries.push_back({entry,std::move(image)}); entry.next=began+entry.config.interval; entry.captured=entries.back().frame->captured;
        }
        if (mode==DetectionMode::pixels) std::stable_sort(entries.begin(),entries.end(),[](const auto& a,const auto& b) { return a.entry.config.interval<b.entry.config.interval; });
        if (mode==DetectionMode::text) ocr_next_=began+text_interval;
        const auto label_queries=reads_labels && labels_next_<=began?label_queries_:std::vector<FixedLabelQuery>{};
        const auto label_revision=label_revision_;
        if (!label_queries.empty()) labels_next_=began+Time(100);
        lock.unlock();
        auto cancelled=[this,revision,text_revision,mode] { return revision_.load()!=revision || (mode==DetectionMode::text && text_revision_.load()!=text_revision); };
        std::vector<TextLine> lines; bool readable=true;
        Reading<InteractionCaption> caption;
        if (mode==DetectionMode::text) {
            ObservedState result;
            bool requested=false,hive_requested=false;
            try {
                for (auto entry=entries.begin();entry!=entries.end();) {
                    std::unique_lock invocation(entry->entry.config.detector->invocation_,std::try_to_lock);
                    if (!invocation.owns_lock()) { entry=entries.erase(entry); continue; }
                    const auto requested=entry->entry.config.detector->text_regions({*frame,{},generation,cancelled,{},nullptr});
                    ++entry;
                    regions.insert(regions.end(),requested.begin(),requested.end());
                    if (regions.size()>64 || std::find(regions.begin(),regions.end(),Rect{0,0,1,1})!=regions.end()) {
                        regions={{0,0,1,1}}; break;
                    }
                }
                if (regions.size()>64) regions={{0,0,1,1}};
                requested=!regions.empty();
                auto prompt=requested?interaction_text_region(*frame):std::optional<Rect>{};
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
                if (hive_requested) caption={Knowledge::absent,{},frame->captured,frame->window,generation};
                if (prompt) {
                    const auto image=interaction_text_image(*frame,*prompt);
                    std::vector<TextLine> isolated;
                    if (const auto label=interaction_label(image)) {
                        isolated.push_back(*label);
                        caption={Knowledge::present,{label->text,interaction_cooldown(*frame,*prompt)},frame->captured,frame->window,generation};
                    }
                    else isolated=prompt_recognition.read(desktop_,image,generation,{{0,0,1,1}},cancelled);
                    for (auto& line:isolated) {
                        line.pixels.x+=std::floor(prompt->x*frame->width)-interaction_text_padding;
                        line.pixels.y+=std::floor(prompt->y*frame->height)-interaction_text_padding;
                    }
                    lines.insert(lines.end(),isolated.begin(),isolated.end());
                } else prompt_recognition.clear();
                if (requested) result.state=read_game_text(*frame,lines,generation);
                // A visible game with no interaction banner is known to be
                // away from the hive prompt, even when there is no OCR text.
                if (hive_requested && !prompt && game_hud_visible(*frame,generation))
                    result.state.at_hive={Knowledge::absent,false,frame->captured,frame->window,generation};
                if (hive_requested) result.state.interaction_prompt={Knowledge::present,prompt.has_value(),frame->captured,frame->window,generation};
            } catch (const std::exception& e) { result.error=e.what(); readable=false; }
            catch (...) { result.error="Could not read game window"; readable=false; }
            result.processing=monotonic_now()-began;
            lock.lock();
            if (!cancelled() && !exiting_ && requested) {
                latest_.state.text=std::move(result.state.text);
                latest_.state.interaction_caption=caption;
                if (result.state.connection.knowledge!=Knowledge::unknown && result.state.connection.captured>=latest_.state.connection.captured)
                    latest_.state.connection=result.state.connection;
                if (hive_requested || result.state.hive_prompt.knowledge==Knowledge::present) {
                    latest_.state.hive_prompt=result.state.hive_prompt;
                    latest_.state.at_hive=result.state.at_hive;
                    latest_.state.interaction_prompt=result.state.interaction_prompt;
                }
                latest_.error=std::move(result.error); latest_.processing=result.processing;
            }
            lock.unlock();
        }
        for (const auto& call : entries) {
            if (!readable || cancelled()) break;
            const auto& entry=call.entry;
            const auto& frame=call.frame;
            std::unique_lock invocation(entry.config.detector->invocation_,std::try_to_lock);
            if (!invocation.owns_lock() || cancelled()) continue;
            DetectionResult reading{{frame->window,generation,frame->captured},{},{}};
            try {
                auto recognize=[&](const Frame& crop) {
                    if (mode!=DetectionMode::text) throw std::logic_error("Pixel detection cannot request OCR");
                    return desktop_.recognize(crop,cancelled);
                };
                reading.value=entry.config.detector->detect({*frame,lines,generation,cancelled,recognize,nullptr,&caption});
            }
            catch (const std::exception& e) { reading.error=std::string(e.what()).substr(0,4096); if (reading.error.empty()) reading.error="Detector failed"; }
            catch (...) { reading.error="Detector failed"; }
            lock.lock();
            if (!cancelled() && !exiting_ && wanted(entry,monotonic_now())) {
                if (entry.observation) {
                    if (reading.value) apply_state_observation(latest_.state,*entry.observation,*reading.value);
                }
                entry.channel->publish(std::move(reading));
            }
            lock.unlock();
        }
        if (!label_queries.empty() && !cancelled()) {
            Reading<TextSnapshot> labels;
            std::string error;
            try {
                labels={Knowledge::present,{frame->width,frame->height,{}},frame->captured,frame->window,generation};
                for (const auto& query:label_queries) {
                    auto found=fixed_ui_labels(*frame,query,labels.value.lines);
                    for (auto& line:found) if (std::none_of(labels.value.lines.begin(),labels.value.lines.end(),[&](const auto& prior) {
                        return prior.text==line.text && prior.pixels==line.pixels;
                    })) labels.value.lines.push_back(std::move(line));
                }
            } catch (const std::exception& e) { labels={}; error=e.what(); }
            catch (...) { labels={}; error="Could not read fixed UI labels"; }
            lock.lock();
            if (!cancelled() && !exiting_ && label_revision==label_revision_) {
                latest_.state.fixed_labels=std::move(labels);
                labels_error_=std::move(error);
            }
            lock.unlock();
        }
        lock.lock();
    }
}
}
