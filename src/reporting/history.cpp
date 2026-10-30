#include "report.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace hive::reporting {
namespace {
constexpr qint64 retention=12*60*60*1000,continuity=15000;
template<class T> void trim(std::deque<T>& entries,qint64 now) {
    while (!entries.empty() && (entries.front().at<now-retention || entries.size()>4322)) entries.pop_front();
}
std::optional<double> percent(std::optional<double> value) {
    return value && std::isfinite(*value) && *value>=0 && *value<=100?value:std::nullopt;
}
QString key(const QString& section,const std::string& id) { return section+QChar(0x1f)+QString::fromStdString(id); }
}
void History::reset(qint64 now) {
    spans_.clear(); samples_.clear(); alerts_.clear(); traces_.clear(); counters_.clear(); honey_.clear(); first_honey_.reset();
    trend_name_.clear(); began_=last_=now; activity_.clear(); running_=false;
}
void History::observe(qint64 now,QString activity,bool running) {
    if (now<last_) reset(now);
    if (!running_ && !running) return;
    if (!running_ && running && spans_.empty()) began_=last_=now;
    if (running_ && now>last_) {
        if (!spans_.empty() && spans_.back().name==activity_ && spans_.back().to==last_) spans_.back().to=now;
        else spans_.push_back({last_,now,activity_});
    }
    last_=now; running_=running; activity_=activity.left(100);
    while (!spans_.empty() && (spans_.front().to<=now-retention || spans_.size()>4096)) spans_.pop_front();
    trim(samples_,now);
    while (!alerts_.empty() && (alerts_.front()<now-retention || alerts_.size()>4096)) alerts_.pop_front();
}
void History::sample(qint64 now,const Gauge& reading) {
    if (reading.name!=trend_name_) { samples_.clear(); trend_name_=reading.name; }
    if (!samples_.empty() && now==samples_.back().at) samples_.back().percent=percent(reading.percent);
    else samples_.push_back({now,percent(reading.percent)});
    trim(samples_,now);
}
void History::sample(qint64 now,const Snapshot& snapshot) {
    sample(now,snapshot.trend);
    for (auto it=traces_.begin();it!=traces_.end();) {
        trim(it.value(),now);
        if (it->empty()) it=traces_.erase(it); else ++it;
    }
    for (auto& count:counters_) trim(count.deltas,now);
    const auto gauges=[&](const QList<Gauge>& values) {
        for (const auto& value:values) {
            const auto id=value.id.isEmpty()?value.name:value.id;
            if (!traces_.contains(id) && traces_.size()>=64) continue;
            auto& trace=traces_[id];
            std::optional<double> level;
            if (value.level && value.level->value && std::isfinite(*value.level->value)
                && *value.level->value>=0 && *value.level->value<=value.level->maximum) level=value.level->value;
            const Sample sample{now,percent(value.percent),percent(value.floor),level};
            if (!trace.empty() && trace.back().at==now) trace.back()=sample;
            else trace.push_back(sample);
            trim(trace,now);
        }
    };
    gauges(snapshot.gauges); gauges(snapshot.buffs);
    const auto counters=[&](const QString& section,const std::vector<Counter>& values) {
        for (const auto& value:values) {
            const auto id=key(section,value.id);
            auto found=counters_.find(id);
            if (found==counters_.end()) { if (counters_.size()<512) counters_.insert(id,Count{value}); continue; }
            auto& count=found.value();
            const auto delta=value.epoch!=count.last.epoch || value.value<count.last.value?value.value:value.value-count.last.value;
            count.session+=delta; count.last=value;
            if (delta) count.deltas.push_back({now,delta});
            trim(count.deltas,now);
        }
    };
    counters({},snapshot.counters);
    for (const auto& section:snapshot.sections) counters(section.id.isEmpty()?section.name:section.id,section.counters);
    const HoneySample reading{now-std::max(qint64(0),snapshot.honey_age_ms),snapshot.honey};
    if (reading.at>=began_) {
        // OCR can finish after a newer sample recorded an unreadable frame.
        // Keep capture order without letting that gap discard a valid balance.
        auto at=std::lower_bound(honey_.begin(),honey_.end(),reading.at,[](const auto& sample,qint64 time) { return sample.at<time; });
        if (at==honey_.end() || at->at!=reading.at) at=honey_.insert(at,reading);
        else if (!at->value) at->value=reading.value;
        if (at->value && (!first_honey_ || at->at<first_honey_->at)) first_honey_=*at;
    }
    trim(honey_,now);
}
void History::alert(qint64 now) {
    alerts_.push_back(now);
    while (!alerts_.empty() && (alerts_.front()<now-retention || alerts_.size()>4096)) alerts_.pop_front();
}
Report History::report(Snapshot snapshot,qint64 session_ms,qint64 window_ms,qint64 now) const {
    Report result; result.snapshot=std::move(snapshot); result.session_ms=session_ms;
    const auto end=running_?now:last_;
    result.tracked_ms=std::max<qint64>(0,end-began_);
    auto start=std::max({began_,end-std::clamp<qint64>(window_ms,0,retention),qint64(0)});
    if (!spans_.empty()) start=std::max(start,spans_.front().from);
    result.window_ms=std::max<qint64>(0,end-start);
    result.window_age_ms=std::max<qint64>(0,now-end);
    std::map<QString,qint64> durations;
    for (const auto& span:spans_) {
        const auto overlap=std::min(end,span.to)-std::max(start,span.from);
        if (overlap>0) durations[span.name]+=overlap;
    }
    if (running_ && now>last_) durations[activity_]+=now-std::max(last_,start);
    for (const auto& [name,time]:durations) result.activities.append({name,time});
    std::sort(result.activities.begin(),result.activities.end(),[](const auto& a,const auto& b) { return a.milliseconds>b.milliseconds; });
    for (const auto& value:samples_) if (value.at>=start && value.at<=end) result.trend.append({value.at-start,value.percent});
    result.alerts=int(std::count_if(alerts_.begin(),alerts_.end(),[&](auto at) { return at>=start && at<=end; }));
    const auto series=[&](const QList<Gauge>& gauges,QList<GaugeHistory>& output) {
        for (const auto& gauge:gauges) {
            GaugeHistory history{gauge}; const auto trace=traces_.constFind(gauge.id.isEmpty()?gauge.name:gauge.id);
            if (trace!=traces_.cend()) {
                const Sample* before=nullptr;
                for (const auto& point:*trace) {
                    if (point.at>end) break;
                    if (point.at>=start) {
                        history.samples.append({point.at-start,point.percent,point.floor,point.level});
                        if (point.level) history.peak=std::max(history.peak.value_or(0),*point.level);
                    }
                    const auto span=before?std::min(end,point.at)-std::max(start,before->at):0;
                    if (before && span>0 && point.at-before->at<=continuity && before->percent && point.percent) {
                        history.observed_ms+=span;
                        if (*before->percent>0) history.active_ms+=span;
                        if (before->floor && point.floor && *before->floor==*point.floor) {
                            history.target_ms+=span;
                            const auto first=*before->percent+(*point.percent-*before->percent)*double(std::max(start,before->at)-before->at)/(point.at-before->at);
                            const auto second=*point.percent, floor=*point.floor;
                            const auto fraction=first<floor && second<floor?1.:first>=floor && second>=floor?0.:
                                first<floor?(floor-first)/(second-first):(floor-second)/(first-second);
                            history.below_ms+=qint64(span*fraction);
                        }
                    }
                    before=&point;
                }
            }
            output.append(std::move(history));
        }
    };
    series(result.snapshot.gauges,result.nectar); series(result.snapshot.buffs,result.buffs);
    for (auto it=counters_.cbegin();it!=counters_.cend();++it) {
        std::uint64_t period=0; for (const auto& delta:it->deltas) if (delta.at>start && delta.at<=end) period+=delta.value;
        result.counts[it.key().section(QChar(0x1f),0,0)].append({QString::fromStdString(it->last.label),period,it->session,QString::fromStdString(it->last.summary)});
    }
    result.honey.current=result.snapshot.honey;
    const HoneySample *first=nullptr,*last=nullptr,*previous=nullptr;
    for (const auto& sample:honey_) {
        if (sample.at>end) break;
        if (sample.at>=start) {
            if (sample.value) { if (!first) first=&sample; last=&sample; }
            std::optional<double> rate;
            if (previous && previous->value && sample.value && sample.at>previous->at
                && sample.at-previous->at<=continuity && *sample.value>=*previous->value)
                rate=double(*sample.value-*previous->value)*1000/(sample.at-previous->at);
            result.honey.rates.append({sample.at-start,rate});
        }
        previous=&sample;
    }
    if (first && last) {
        if (!result.honey.current && end-last->at<=2*sample_interval_ms) result.honey.current=last->value;
        result.honey.period=qint64(*last->value)-qint64(*first->value); result.honey.observed_ms=last->at-first->at;
        if (first_honey_) {
            result.honey.session=qint64(*last->value)-qint64(*first_honey_->value);
            if (session_ms>0 && end-last->at<=30000) result.honey.hourly=double(*result.honey.session)*3600000/session_ms;
            result.honey.baseline_delay=first_honey_->at-began_+std::max<qint64>(0,session_ms-result.tracked_ms);
        }
    }
    return result;
}
}
