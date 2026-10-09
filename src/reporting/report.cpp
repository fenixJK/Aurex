#include "report.hpp"
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <limits>

namespace hive::reporting {
namespace {

QColor accent(int index) {
    static const QColor colors[]{"#91d2b6","#e9be6b","#8aace0","#bb9ddd","#e69b8e","#70c8d2","#d6d68c","#d995c6","#78b8a4","#d4a57c","#a5a6e4","#b2c1c8"};
    return colors[index%12];
}
void font(QPainter& painter,int size,bool bold=false) {
    QFont value=painter.font(); value.setPixelSize(size); value.setWeight(bold?QFont::DemiBold:QFont::Normal); painter.setFont(value);
}
void text(QPainter& painter,QRectF rect,QString value,int size,QColor color=QColor("#e6eeee"),bool bold=false,int alignment=Qt::AlignLeft|Qt::AlignVCenter) {
    font(painter,size,bold); painter.setPen(color);
    value=redact(value).simplified(); painter.drawText(rect,alignment,painter.fontMetrics().elidedText(value,Qt::ElideRight,int(rect.width())));
}
void card(QPainter& painter,QRectF rect) {
    painter.setPen(QColor("#2a3c43")); painter.setBrush(QColor("#17282e")); painter.drawRoundedRect(rect,14,14);
}
void bar(QPainter& painter,QRectF rect,std::optional<double> value,QColor color) {
    painter.setPen(Qt::NoPen); painter.setBrush(QColor("#2b3e44")); painter.drawRoundedRect(rect,rect.height()/2,rect.height()/2);
    if (value && std::isfinite(*value)) {
        const auto width=rect.width()*std::clamp(*value,0.,100.)/100.;
        if (width>0) { rect.setWidth(width); painter.setBrush(color); painter.drawRoundedRect(rect,rect.height()/2,rect.height()/2); }
    }
}
}
QString redact(QString value) {
    static const QRegularExpression links(R"((?:https?://|roblox://|aurex://join/)[^\s<>"']+)",QRegularExpression::CaseInsensitiveOption);
    return value.replace(links,"[link]");
}
QString duration(qint64 milliseconds) {
    const auto seconds=std::max<qint64>(0,milliseconds)/1000;
    if (seconds<60) return QString("%1s").arg(seconds);
    const auto minutes=seconds/60;
    if (minutes<60) return seconds%60?QString("%1m %2s").arg(minutes).arg(seconds%60):QString("%1m").arg(minutes);
    return minutes%60?QString("%1h %2m").arg(minutes/60).arg(minutes%60):QString("%1h").arg(minutes/60);
}
QString amount(double value) {
    if (!std::isfinite(value)) return "—";
    static const QStringList suffixes{"","K","M","B","T","Qa"};
    int unit=0; while (std::abs(value)>=1000 && unit+1<suffixes.size()) { value/=1000; ++unit; }
    auto result=QString::number(value,'f',unit?2:0);
    if (unit) { while (result.endsWith('0')) result.chop(1); if (result.endsWith('.')) result.chop(1); }
    return result+suffixes[unit];
}
namespace {
void plot_line(QPainter& p,QRectF plot,const QList<Sample>& samples,qint64 window,double low,double high,QColor color,std::optional<double> Sample::* value=&Sample::percent) {
    QPainterPath path; bool connected=false; qint64 last=-1;
    for (const auto& point:samples) {
        const auto& reading=point.*value;
        if (!reading || !std::isfinite(*reading)) { connected=false; continue; }
        const QPointF xy(plot.left()+plot.width()*std::clamp(double(point.at)/std::max<qint64>(1,window),0.,1.),
                         plot.bottom()-plot.height()*std::clamp((*reading-low)/(high-low),0.,1.));
        if (!connected || point.at-last>30000) path.moveTo(xy); else path.lineTo(xy);
        last=point.at; connected=true;
    }
    p.setPen(QPen(color,1.6)); p.setBrush(Qt::NoBrush); p.drawPath(path);
}
void time_axis(QPainter& p,QRectF plot,qint64 window,const QDateTime& end) {
    if (window<=0) return;
    for (int i=0;i<=4;++i) {
        const auto alignment=(i==0?Qt::AlignLeft:i==4?Qt::AlignRight:Qt::AlignHCenter)|Qt::AlignVCenter;
        const auto x=plot.left()+plot.width()*i/4-(i==0?0:i==4?80:40);
        text(p,{x,plot.bottom()+6,80,18},end.addMSecs(window*i/4-window).toString("HH:mm"),11,QColor("#8ba3ac"),false,alignment);
    }
}
void honey_backpack_chart(QPainter& p,QRectF plot,const Report& report) {
    double low=0,high=1; bool honey=false,backpack=false;
    for (const auto& point:report.honey.rates) if (point.percent && std::isfinite(*point.percent)) {
        low=std::min(low,*point.percent); high=std::max(high,*point.percent); honey=true;
    }
    for (const auto& point:report.trend) if (point.percent && std::isfinite(*point.percent)) backpack=true;
    const double magnitude=std::pow(10.,std::floor(std::log10(high-low)));
    low=std::floor(low/magnitude)*magnitude;
    high=std::ceil(high/magnitude)*magnitude;
    for (int i=0;i<3;++i) {
        const auto y=plot.top()+i*plot.height()/2;
        p.setPen(QColor("#435c66")); p.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));
        text(p,{plot.left()-48,y-9,40,18},QString::number(100-i*50)+"%",11,accent(0),false,Qt::AlignRight|Qt::AlignVCenter);
        if (honey) text(p,{plot.right()+8,y-9,48,18},amount(high-i*(high-low)/2),11,accent(1));
    }
    for (int i=1;i<4;++i) {
        const auto x=plot.left()+plot.width()*i/4;
        p.setPen(QPen(QColor("#3d5660"),1,Qt::DashLine)); p.drawLine(QPointF(x,plot.top()),QPointF(x,plot.bottom()));
    }
    p.save(); p.setClipRect(plot); p.setPen(Qt::NoPen); p.setRenderHint(QPainter::Antialiasing,false);
    QLinearGradient fill(plot.topLeft(),plot.bottomLeft());
    fill.setColorAt(0,QColor("#e9be6b")); fill.setColorAt(1,QColor("#857043")); p.setBrush(fill);
    qint64 previous=0;
    for (const auto& point:report.honey.rates) {
        const auto elapsed=point.at-previous; previous=point.at;
        if (!point.percent || !std::isfinite(*point.percent) || *point.percent<=0 || report.window_ms<=0) continue;
        const auto x=plot.left()+plot.width()*double(point.at)/report.window_ms;
        const auto height=plot.height()*std::clamp((*point.percent-low)/(high-low),0.,1.);
        const auto width=std::clamp(plot.width()*double(std::max<qint64>(0,elapsed))/report.window_ms,.5,plot.width());
        p.drawRect(QRectF(x-width,plot.bottom()-height,width,height));
    }
    p.restore();
    plot_line(p,plot,report.trend,report.window_ms,0,100,accent(0));
    if (!honey && !backpack) text(p,plot,"Waiting for readings",15,QColor("#8ba3ac"),false,Qt::AlignCenter);
}
void gauge_sparkline(QPainter& p,QRectF plot,const QList<Sample>& samples,qint64 window,QColor color,std::optional<double> floor) {
    if (floor) {
        p.setPen(QPen(QColor("#d59c87"),1,Qt::DashLine)); const auto y=plot.bottom()-plot.height()*std::clamp(*floor,0.,100.)/100;
        p.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));
    }
    plot_line(p,plot,samples,window,0,100,color);
}

}
QList<CounterResult> summarize(const Report& report,const Section& section) {
    QList<CounterResult> result;
    const auto add=[](std::uint64_t a,std::uint64_t b) { return a+std::min(b,std::numeric_limits<std::uint64_t>::max()-a); };
    for (const auto& count:report.counts.value(section.id.isEmpty()?section.name:section.id)) {
        const auto label=count.summary.isEmpty()?QString("Completed"):count.summary;
        auto found=std::find_if(result.begin(),result.end(),[&](const auto& group) { return group.label==label; });
        if (found==result.end()) result.append({label,count.period,count.session});
        else { found->period=add(found->period,count.period); found->session=add(found->session,count.session); }
    }
    if (result.size()>2) {
        CounterResult other{"Other"};
        while (result.size()>1) { const auto count=result.takeLast(); other.period=add(other.period,count.period); other.session=add(other.session,count.session); }
        result.append(other);
    }
    return result;
}
QImage render_report(const Report& report) {
    constexpr int width=1000,margin=24,content=952,buffs_y=276,buff_spacing=58;
    const auto& snapshot=report.snapshot;
    const auto end=report.created.addMSecs(-report.window_age_ms);
    QList<const GaugeHistory*> buffs;
    for (const auto& trace:report.buffs)
        if (trace.current.level)
            buffs.append(&trace);
    const int buff_rows=int(buffs.size());
    const double plot_left=std::any_of(buffs.begin(),buffs.end(),[](const auto* trace) { return trace->current.icon.isNull(); })?144:92;
    const double plot_width=880-plot_left;
    QList<const Section*> panels;
    for (const auto& section:snapshot.sections)
        if (std::any_of(section.metrics.begin(),section.metrics.end(),[](const auto& metric) { return metric.progress.has_value(); })) panels.append(&section);
    const int nectar_rows=std::min(6,int(report.nectar.size()));
    const int nectar_height=nectar_rows?36+nectar_rows*29:0;
    int panel_height=0;
    for (const auto* section:panels.first(std::min(qsizetype(4),panels.size()))) {
        int rows=0; for (const auto& metric:section->metrics) if (metric.progress) ++rows;
        panel_height+=(panel_height?12:0)+34+std::min(6,rows)*43;
    }
    const int chart_bottom=buff_rows?buffs_y+20+(buff_rows-1)*buff_spacing+40:264;
    const int buff_height=buff_rows?chart_bottom-buffs_y+36:32;
    const int activity_y=buffs_y+buff_height+14;
    const int details_y=activity_y+48;
    QList<CounterResult> counts;
    for (const auto& section:snapshot.sections) for (auto count:summarize(report,section)) {
        if (!count.session && !count.period) continue;
        count.label=section.name+(count.label=="Completed"?QString{}:" · "+count.label.toLower()); counts.append(count);
    }
    for (const auto& count:report.counts.value({})) if (count.session || count.period) counts.append(count);
    const int shown=std::min(10,int(counts.size()));
    const int stats_height=shown?38+shown*22:0;
    const int bottom=details_y+std::max({nectar_height,panel_height,stats_height});
    const bool partial=report.tracked_ms+30000<report.session_ms;
    QStringList notes;
    if (report.alerts) notes.append(QString("%1 alerts").arg(report.alerts));
    if (partial) notes.append("Tracked "+duration(report.tracked_ms)+" of this session");
    else if (report.honey.baseline_delay>30000) notes.append("Honey starts "+duration(report.honey.baseline_delay)+" into the session");
    if (std::any_of(buffs.begin(),buffs.end(),[&](const auto* trace) { return report.window_ms && trace->observed_ms<report.window_ms*9/10; }))
        notes.append("* Uptime uses readable samples; gaps are unknown");
    if (panels.size()>4 || counts.size()>shown) notes.append("Additional activity omitted");
    QImage image(width,bottom+18+(notes.empty()?0:20),QImage::Format_RGB32); image.fill(QColor("#101d23"));
    QPainter p(&image); p.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing|QPainter::SmoothPixmapTransform);
    p.fillRect(0,0,width,4,QColor("#d4b16c"));
    text(p,{margin,13,160,28},"AUREX",22,QColor("#e7cc8f"),true);
    const auto period=end.addMSecs(-report.window_ms).toString("d MMM · HH:mm")+" – "+end.toString("HH:mm");
    text(p,{195,15,420,24},period,14,QColor("#a1b7bd"));
    text(p,{756,15,220,24},"Session "+duration(report.session_ms),14,QColor("#a1b7bd"),false,Qt::AlignRight|Qt::AlignVCenter);
    const auto money=[](auto value) { return value?amount(double(*value)):QString("—"); };
    const auto gain=[&](auto value) { return value && *value>0?"+"+money(value):money(value); };
    card(p,{margin,50,content,70});
    const QStringList labels{"Hourly average","Current honey","Honey · "+duration(report.window_ms),partial?"Tracked session gain":"Session gain"};
    const QStringList values{money(report.honey.hourly),money(report.honey.current),gain(report.honey.period),gain(report.honey.session)};
    for (int i=0;i<4;++i) {
        const double x=margin+18+i*content/4.;
        text(p,{x,57,220,21},labels[i],13,QColor("#a1b7bd"));
        text(p,{x,80,220,32},values[i],29,i==0?accent(1):i==2?accent(report.honey.period && *report.honey.period<0?4:0):QColor("#e6eeee"),true);
    }
    card(p,{margin,132,content,double(buffs_y+buff_height-132)});
    for (int i=0;i<2;++i) {
        const double x=40+i*122;
        if (i) {
            p.fillRect(QRectF(x,148,3,8),accent(i)); p.fillRect(QRectF(x+5,144,3,12),accent(i)); p.fillRect(QRectF(x+10,150,3,6),accent(i));
        } else { p.setPen(QPen(accent(i),2)); p.drawLine(QPointF(x,152),QPointF(x+16,152)); }
        text(p,{x+23,141,130,22},i?"Honey / sec":"Backpack",12,accent(i));
    }
    honey_backpack_chart(p,{plot_left,174,plot_width,90},report);
    qint64 total=0; for (const auto& activity:report.activities) total+=activity.milliseconds;
    const QRectF activity_bar(margin,activity_y,content,6); bar(p,activity_bar,{},{});
    if (total) {
        double x=activity_bar.x(); p.save(); QPainterPath clip; clip.addRoundedRect(activity_bar,3,3); p.setClipPath(clip);
        for (int i=0;i<report.activities.size();++i) { const auto part=activity_bar.width()*report.activities[i].milliseconds/total; p.fillRect(QRectF(x,activity_bar.y(),part,6),accent(i)); x+=part; }
        p.restore();
        const int shown=std::min(6,int(report.activities.size())); const double cell=double(content)/std::max(1,shown);
        for (int i=0;i<shown;++i) {
            const auto& activity=report.activities[i];
            text(p,{margin+i*cell,double(activity_y+12),cell-8,20},activity.name+" "+QString::number(std::lround(100.*activity.milliseconds/total))+"%",12,accent(i),true);
        }
    }
    if (buff_rows) {
        p.setPen(QColor("#2a3c43")); p.drawLine(QPointF(40,buffs_y),QPointF(960,buffs_y));
        text(p,{906,buffs_y+2,54,18},"Uptime",10,QColor("#8ba3ac"),false,Qt::AlignRight|Qt::AlignVCenter);
    }
    const auto uptime=[&](const GaugeHistory& trace) {
        const bool incomplete=report.window_ms && trace.observed_ms<report.window_ms*9/10;
        return trace.observed_ms?QString::number(100.*trace.active_ms/trace.observed_ms,'f',0)+"%"+(incomplete?"*":""):QString("—");
    };
    for (int i=0;i<buff_rows;++i) {
        const auto& trace=*buffs[i];
        const auto color=trace.current.color.isValid()?trace.current.color:accent(i);
        const double y=buffs_y+20+i*buff_spacing;
        const QRectF plot(plot_left,y,plot_width,40);
        const double high=std::max(1.,trace.current.level->maximum);
        for (int grid=0;grid<3;++grid) {
            const auto gy=plot.top()+grid*plot.height()/2;
            p.setPen(QPen(QColor(grid==1?"#364e58":"#435c66"),1));
            p.drawLine(QPointF(plot.left(),gy),QPointF(plot.right(),gy));
        }
        for (int tick=1;tick<4;++tick) {
            const auto x=plot.left()+plot.width()*tick/4;
            p.setPen(QPen(QColor("#3d5660"),1,Qt::DashLine)); p.drawLine(QPointF(x,plot.top()),QPointF(x,plot.bottom()));
        }
        plot_line(p,plot,trace.samples,report.window_ms,0,high,color,&Sample::level);
        if (trace.current.icon.isNull()) text(p,{36,y,double(plot_left-44),40},trace.current.name,11,color,true);
        else {
            const auto size=trace.current.icon.size().scaled(36,36,Qt::KeepAspectRatio);
            p.drawImage(QRectF(42+(36-size.width())/2.,y+(40-size.height())/2.,size.width(),size.height()),trace.current.icon);
        }
        text(p,{916,y,44,40},uptime(trace),12,QColor("#a1b7bd"),true,Qt::AlignRight|Qt::AlignVCenter);
        text(p,{888,y-9,26,18},high==1?"On":"×"+QString::number(high,'g',3),11,color);
        if (!trace.peak) text(p,plot,"Waiting for readings",11,QColor("#8ba3ac"),false,Qt::AlignCenter);
    }
    time_axis(p,{plot_left,double(chart_bottom)-26,plot_width,26},report.window_ms,end);
    int side_y=details_y;
    if (nectar_rows) {
        card(p,{24,double(side_y),304,double(nectar_height)});
        text(p,{40,double(side_y+8),272,24},"Nectar",15,QColor("#dce8e9"),true);
        for (int i=0;i<nectar_rows;++i) {
            const auto& trace=report.nectar[i]; const auto& gauge=trace.current; const double y=side_y+36+i*29;
            const auto color=gauge.color.isValid()?gauge.color:accent(i);
            text(p,{40,y,106,23},gauge.name,13,QColor("#a1b7bd"));
            gauge_sparkline(p,{151,y+4,102,14},trace.samples,report.window_ms,color,gauge.floor);
            text(p,{261,y,51,23},gauge.percent?QString::number(*gauge.percent,'f',0)+"%":"—",16,color,true,Qt::AlignRight|Qt::AlignVCenter);
        }
    }
    for (const auto* section:panels.first(std::min(qsizetype(4),panels.size()))) {
        QList<Metric> metrics; for (const auto& metric:section->metrics) if (metric.progress && metrics.size()<6) metrics.append(metric);
        const int height=34+metrics.size()*43;
        card(p,{340,double(side_y),320,double(height)});
        text(p,{356,double(side_y+7),288,24},section->name,15,QColor("#dce8e9"),true);
        for (int i=0;i<metrics.size();++i) {
            const auto& metric=metrics[i]; const double y=side_y+35+i*43;
            text(p,{356,y,106,20},metric.label,13,QColor("#a1b7bd"));
            text(p,{468,y,176,20},metric.value,13,QColor("#e6eeee"),true,Qt::AlignRight|Qt::AlignVCenter);
            bar(p,{356,y+26,288,4},metric.progress,metric.color.isValid()?metric.color:accent(i));
        }
        side_y+=height+12;
    }
    if (shown) {
        card(p,{672,double(details_y),304,double(stats_height)});
        text(p,{688,double(details_y+7),140,22},"Totals",15,QColor("#dce8e9"),true);
        text(p,{837,double(details_y+9),58,20},duration(report.window_ms),11,QColor("#8ba3ac"),false,Qt::AlignRight|Qt::AlignVCenter);
        text(p,{902,double(details_y+9),58,20},"Session",11,QColor("#8ba3ac"),false,Qt::AlignRight|Qt::AlignVCenter);
        for (int i=0;i<shown;++i) {
            const auto& count=counts[i]; const double y=details_y+32+i*22;
            text(p,{688,y,142,20},count.label,12,QColor("#a1b7bd"));
            text(p,{837,y,58,20},amount(double(count.period)),13,QColor("#dce8e9"),true,Qt::AlignRight|Qt::AlignVCenter);
            text(p,{902,y,58,20},amount(double(count.session)),12,QColor("#8ba3ac"),false,Qt::AlignRight|Qt::AlignVCenter);
        }
    }
    if (!notes.empty()) text(p,{margin,double(bottom+3),content,20},notes.join("   ·   "),11,QColor("#7f99a3"));
    return image;
}
}
