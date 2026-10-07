#pragma once
#include "hive/core/statistics.hpp"
#include <QColor>
#include <QDateTime>
#include <QImage>
#include <QList>
#include <QMap>
#include <QString>
#include <deque>
#include <optional>

namespace hive::reporting {
struct Metric { QString label,value; std::optional<double> progress; QColor color; };
struct Section { QString name; QList<Metric> metrics; QString id; std::vector<Counter> counters; };
struct Level { std::optional<double> value; double maximum{100}; };
struct Gauge { QString name; std::optional<double> percent; QString id; std::optional<double> floor; std::optional<Level> level; QImage icon; QColor color; };
struct Snapshot {
    QString state,activity;
    QList<Section> sections;
    QList<Gauge> gauges;
    Gauge trend;
    std::optional<std::uint64_t> honey;
    QList<Gauge> buffs;
    std::vector<Counter> counters;
};
struct ActivityTime { QString name; qint64 milliseconds{}; };
struct Sample { qint64 at{}; std::optional<double> percent; std::optional<double> floor; std::optional<double> level; };
struct GaugeHistory { Gauge current; QList<Sample> samples; qint64 observed_ms{},below_ms{},active_ms{},target_ms{}; std::optional<double> peak; };
struct CounterResult { QString label; std::uint64_t period{},session{}; QString summary; };
struct HoneyReport {
    std::optional<std::uint64_t> current;
    std::optional<qint64> period,session;
    std::optional<double> hourly;
    QList<Sample> rates;
    qint64 observed_ms{},baseline_delay{};
};
struct Report {
    Snapshot snapshot;
    QDateTime created{QDateTime::currentDateTime()};
    qint64 session_ms{},window_ms{},tracked_ms{};
    QList<ActivityTime> activities;
    QList<Sample> trend;
    int alerts{};
    HoneyReport honey;
    QList<GaugeHistory> nectar,buffs;
    QMap<QString,QList<CounterResult>> counts;
    qint64 window_age_ms{};
};
QString redact(QString text);
QString duration(qint64 milliseconds);
QString amount(double value);
QList<CounterResult> summarize(const Report& report,const Section& section);
QImage render_report(const Report& report);
class History {
public:
    void reset(qint64 now);
    void observe(qint64 now,QString activity,bool running);
    void sample(qint64 now,const Gauge& reading);
    void sample(qint64 now,const Snapshot& snapshot);
    void alert(qint64 now);
    Report report(Snapshot snapshot,qint64 session_ms,qint64 window_ms,qint64 now) const;
private:
    struct Span { qint64 from{},to{}; QString name; };
    struct Delta { qint64 at{}; std::uint64_t value{}; };
    struct Count { Counter last; std::uint64_t session{}; std::deque<Delta> deltas; };
    struct HoneySample { qint64 at{}; std::optional<std::uint64_t> value; };
    std::deque<Span> spans_;
    std::deque<Sample> samples_;
    std::deque<qint64> alerts_;
    QMap<QString,std::deque<Sample>> traces_;
    QMap<QString,Count> counters_;
    std::deque<HoneySample> honey_;
    std::optional<HoneySample> first_honey_;
    QString trend_name_;
    qint64 began_{},last_{};
    bool running_{};
    QString activity_;
};
}
