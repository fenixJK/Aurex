#include <QImageReader>
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

struct Palette {
    QColor background,panel,border,heading;
    QColor foreground{"#e6eeee"},muted{"#a1b7bd"},subtle{"#8ba3ac"};
    QColor grid{"#435c66"},grid_secondary{"#364e58"},grid_vertical{"#3d5660"},track{"#2b3e44"},divider{"#2a3c43"};
    bool light{};
    QColor ink(QColor color) const {
        if (!light) return color;
        const auto luminance=[](QColor c) {
            const auto linear=[](double v) { return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4); };
            return .2126*linear(c.redF())+.7152*linear(c.greenF())+.0722*linear(c.blueF());
        };
        const auto surface=std::min(luminance(background),luminance(panel));
        while ((surface+.05)/(luminance(color)+.05)<4.5) color=color.darker(110);
        return color;
    }
};
Palette palette(ReportTheme theme) {
    switch (theme) {
    case ReportTheme::midnight:return {"#141827","#202638","#3a445a","#a6b9e8"};
    case ReportTheme::graphite:return {"#17191c","#25282d","#41464e","#c9d1da"};
    case ReportTheme::black:return {"#08090b","#14161a","#30343b","#f0f2f5",
        "#e8ebf0","#b8bdc7","#969eac","#3b424d","#303641","#343b46","#2b3039","#292f38"};
    case ReportTheme::plum:return {"#201824","#302435","#58425f","#e4b7ec",
        "#f0e7f2","#c8b3ce","#af96b7","#67516f","#503d57","#5b4663","#4e3b55","#4b3752"};
    case ReportTheme::paper:return {"#f3f4f6","#ffffff","#d1d5db","#283447",
        "#202b3b","#4f5d6e","#5d6a7a","#c5ccd5","#d7dde5","#d1d7e0","#e2e7ed","#d7dde5",true};
    default:return {"#101d23","#17282e","#2a3c43","#e7cc8f"};
    }
}
QColor accent(int index,const Palette& palette) {
    static const QColor colors[]{"#91d2b6","#e9be6b","#8aace0","#bb9ddd","#e69b8e","#70c8d2","#d6d68c","#d995c6","#78b8a4","#d4a57c","#a5a6e4","#b2c1c8"};
    return palette.ink(colors[index%12]);
}
void font(QPainter& painter,int size,bool bold=false) {
    QFont value=painter.font(); value.setPixelSize(size); value.setWeight(bold?QFont::DemiBold:QFont::Normal); painter.setFont(value);
}
void text(QPainter& painter,QRectF rect,QString value,int size,QColor color,bool bold=false,int alignment=Qt::AlignLeft|Qt::AlignVCenter) {
    font(painter,size,bold); painter.setPen(color);
    value=redact(value).simplified(); painter.drawText(rect,alignment,painter.fontMetrics().elidedText(value,Qt::ElideRight,int(rect.width())));
}
void card(QPainter& painter,QRectF rect,const Palette& colors) {
    painter.setPen(colors.border); painter.setBrush(colors.panel); painter.drawRoundedRect(rect,14,14);
}
void bar(QPainter& painter,QRectF rect,std::optional<double> value,QColor color,const Palette& colors) {
    painter.setPen(Qt::NoPen); painter.setBrush(colors.track); painter.drawRoundedRect(rect,rect.height()/2,rect.height()/2);
    if (value && std::isfinite(*value)) {
        const auto width=rect.width()*std::clamp(*value,0.,100.)/100.;
        if (width>0) { rect.setWidth(width); painter.setBrush(color); painter.drawRoundedRect(rect,rect.height()/2,rect.height()/2); }
    }
}
}
const QList<ReportThemeChoice>& report_themes() {
    static const QList<ReportThemeChoice> choices=[] {
        QList<ReportThemeChoice> result;
        for (const auto& [id,name]:std::initializer_list<std::pair<ReportTheme,const char*>>{
            {ReportTheme::forest,"Forest (default)"},{ReportTheme::midnight,"Midnight"},
            {ReportTheme::graphite,"Graphite"},{ReportTheme::black,"Black"},
            {ReportTheme::plum,"Plum"},{ReportTheme::paper,"Paper"}})
            result.append({id,QString::fromUtf8(name),palette(id).background});
        return result;
    }();
    return choices;
}
bool supported_report_theme(ReportTheme theme) {
    const auto& choices=report_themes();
    return std::any_of(choices.begin(),choices.end(),[&](const auto& choice) { return choice.id==theme; });
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
void time_axis(QPainter& p,QRectF plot,qint64 window,const QDateTime& end,const Palette& colors) {
    if (window<=0) return;
    for (int i=0;i<=4;++i) {
        const auto alignment=(i==0?Qt::AlignLeft:i==4?Qt::AlignRight:Qt::AlignHCenter)|Qt::AlignVCenter;
        const auto x=plot.left()+plot.width()*i/4-(i==0?0:i==4?80:40);
        text(p,{x,plot.bottom()+6,80,18},end.addMSecs(window*i/4-window).toString("HH:mm"),11,colors.subtle,false,alignment);
    }
}
struct HoneyScale { double low{},high{1}; bool observed{}; };
HoneyScale honey_scale(const Report& report) {
    HoneyScale scale;
    auto& [low,high,honey]=scale;
    for (const auto& point:report.honey.rates) if (point.percent && std::isfinite(*point.percent)) {
        low=std::min(low,*point.percent); high=std::max(high,*point.percent); honey=true;
    }
    const double magnitude=std::pow(10.,std::floor(std::log10(high-low)));
    low=std::floor(low/magnitude)*magnitude;
    high=std::ceil(high/magnitude)*magnitude;
    return scale;
}
void honey_backpack_chart(QPainter& p,QRectF plot,const Report& report,const HoneyScale& scale,const Palette& colors) {
    const auto [low,high,honey]=scale;
    const bool backpack=std::any_of(report.trend.begin(),report.trend.end(),[](const auto& point) { return point.percent && std::isfinite(*point.percent); });
    for (int i=0;i<3;++i) {
        const auto y=plot.top()+i*plot.height()/2;
        p.setPen(colors.grid); p.drawLine(QPointF(plot.left(),y),QPointF(plot.right(),y));
        text(p,{plot.left()-48,y-9,40,18},QString::number(100-i*50)+"%",11,accent(0,colors),false,Qt::AlignRight|Qt::AlignVCenter);
        if (honey) text(p,{plot.right()+6,y-9,960-plot.right()-6,18},amount(high-i*(high-low)/2),11,accent(1,colors),false,Qt::AlignRight|Qt::AlignVCenter);
    }
    for (int i=1;i<4;++i) {
        const auto x=plot.left()+plot.width()*i/4;
        p.setPen(QPen(colors.grid_vertical,1,Qt::DashLine)); p.drawLine(QPointF(x,plot.top()),QPointF(x,plot.bottom()));
    }
    p.save(); p.setClipRect(plot); p.setPen(Qt::NoPen); p.setRenderHint(QPainter::Antialiasing,false);
    QLinearGradient fill(plot.topLeft(),plot.bottomLeft());
    fill.setColorAt(0,accent(1,colors)); fill.setColorAt(1,colors.ink(QColor("#857043"))); p.setBrush(fill);
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
    plot_line(p,plot,report.trend,report.window_ms,0,100,accent(0,colors));
    if (!honey && !backpack) text(p,plot,"Waiting for readings",15,colors.subtle,false,Qt::AlignCenter);
}
void activity_pie(QPainter& p,QRectF area,const Report& report,const Palette& colors) {
    qint64 total=0;
    for (const auto& activity:report.activities) total+=std::max(qint64(0),activity.milliseconds);
    const QRectF pie(area.left(),area.top()+23,88,88);
    text(p,{area.left(),area.top(),area.width(),20},"Activity · "+duration(report.window_ms),12,colors.muted);
    p.setPen(Qt::NoPen); p.setBrush(colors.track); p.drawEllipse(pie);
    double angle=90;
    const double row_height=std::min(19.,96./std::max(1,static_cast<int>(report.activities.size())));
    for (qsizetype i=0;i<report.activities.size();++i) {
        const auto& activity=report.activities[i];
        const auto fraction=total?double(std::max(qint64(0),activity.milliseconds))/total:0.;
        p.setPen(QPen(colors.panel,1)); p.setBrush(accent(i,colors));
        p.drawPie(pie,int(std::lround(angle*16)),-int(std::lround(fraction*360*16))); angle-=fraction*360;
        const double y=area.top()+23+i*row_height;
        p.fillRect(QRectF(area.left()+105,y+(row_height-8)/2,8,8),accent(i,colors));
        text(p,{area.left()+122,y,area.width()-190,row_height},activity.name,12,colors.muted);
        text(p,{area.right()-62,y,62,row_height},QString::number(std::lround(fraction*100))+"%",12,colors.foreground,true,Qt::AlignRight|Qt::AlignVCenter);
    }
    if (!total) text(p,{pie.right()+16,pie.top(),area.width()-104,pie.height()},"No activity recorded",12,colors.subtle);
}
void nectar_summary(QPainter& p,QRectF area,const QList<GaugeHistory>& nectar,const Palette& colors) {
    card(p,area,colors);
    const auto count=std::min(qsizetype(6),nectar.size());
    text(p,{area.left()+16,area.top()+7,72,20},"Nectar",13,colors.foreground,true);
    text(p,{area.left()+16,area.top()+28,72,18},"Current",11,colors.subtle);
    const double cell=(area.width()-100)/count;
    for (qsizetype i=0;i<count;++i) {
        const auto& gauge=nectar[i].current;
        const double x=area.left()+100+i*cell,w=cell-18;
        const auto color=gauge.color.isValid()?colors.ink(gauge.color):accent(i,colors);
        text(p,{x,area.top()+6,w,19},gauge.name,12,colors.muted);
        text(p,{x,area.top()+25,w,20},gauge.percent?QString::number(*gauge.percent,'f',0)+"%":"—",16,color,true);
        bar(p,{x,area.top()+49,w,4},gauge.percent,color,colors);
    }
}
double buff_ceiling(const GaugeHistory& trace) {
    const auto maximum=std::max(1.,trace.current.level->maximum);
    if (maximum<=10 || !trace.peak) return maximum;
    const auto peak=std::clamp(*trace.peak,1.,maximum);
    const auto magnitude=std::pow(10.,std::floor(std::log10(peak)));
    for (double step:{1.,2.,5.,10.})
        if (step*magnitude>=peak) return std::min(maximum,std::max(10.,step*magnitude));
    return maximum;
}
void activity_strip(QPainter& p,QRectF plot,const QList<Sample>& samples,qint64 window,QColor color) {
    p.save(); p.setRenderHint(QPainter::Antialiasing,false);
    p.setPen(Qt::NoPen); p.setBrush(color);
    for (qsizetype i=0;i<samples.size();++i) {
        const auto& point=samples[i];
        if (!point.level || *point.level<=0) continue;
        const auto next=i+1<samples.size()?samples[i+1].at:point.at;
        const auto end=next-point.at<=30000 && i+1<samples.size() && samples[i+1].level?next:point.at;
        const auto x=plot.left()+plot.width()*std::clamp(double(point.at)/std::max<qint64>(1,window),0.,1.);
        const auto right=plot.left()+plot.width()*std::clamp(double(end)/std::max<qint64>(1,window),0.,1.);
        p.drawRect(QRectF(x,plot.top(),std::min(std::max(1.,right-x),plot.right()-x),plot.height()));
    }
    p.restore();
}
struct BuffRow {
    QList<const GaugeHistory*> traces;
    double top{},height{},ceiling{};
};
QList<BuffRow> buff_rows(const QList<const GaugeHistory*>& buffs,double top,int height) {
    QList<BuffRow> rows;
    for (const auto* trace:buffs) {
        const auto& group=trace->current.chart_group;
        auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& row) {
            return !group.isEmpty() && row.traces.front()->current.chart_group==group
                && row.traces.front()->current.level->maximum==trace->current.level->maximum;
        });
        if (row==rows.end()) rows.append({{trace}});
        else row->traces.append(trace);
    }
    for (auto& row:rows)
        for (const auto* trace:row.traces) row.ceiling=std::max(row.ceiling,buff_ceiling(*trace));
    std::stable_partition(rows.begin(),rows.end(),[](const auto& row) { return row.ceiling!=1; });
    for (auto& row:rows) {
        row.top=top;
        row.height=row.ceiling==1?44:row.traces.front()->current.level->maximum>10?66:58;
        row.height=std::max(row.height,14.*row.traces.size()+16);
        row.height=std::max(row.height*std::clamp(height,75,150)/100.,14.*row.traces.size()+(row.ceiling==1?24:34));
        top+=row.height;
    }
    return rows;
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
QImage load_report_background(const QString& path) {
    if (path.isEmpty()) return {};
    QImageReader reader(path); reader.setAutoTransform(true);
    if (reader.size().width()>2048 || reader.size().height()>2048)
        reader.setScaledSize(reader.size().scaled(2048,2048,Qt::KeepAspectRatio));
    return reader.read();
}
QRectF report_background_crop(QSize image,QSize report,const ReportAppearance& style) {
    if (image.isEmpty() || report.isEmpty()) return {};
    const double scale=std::max(double(report.width())/image.width(),double(report.height())/image.height())
        *std::clamp(style.background_zoom,ReportAppearance::min_background_zoom,ReportAppearance::max_background_zoom)/100.;
    const QSizeF crop(report.width()/scale,report.height()/scale);
    const auto position=[](double value) { return std::isfinite(value)?std::clamp(value,0.,1.):.5; };
    return {(image.width()-crop.width())*position(style.background_x),
        (image.height()-crop.height())*position(style.background_y),crop.width(),crop.height()};
}
QImage render_report(const Report& report,const QImage* cached_background) {
    constexpr int width=1000,margin=24,content=952;
    const auto& snapshot=report.snapshot;
    const auto end=report.created.addMSecs(-report.window_age_ms);
    QList<const GaugeHistory*> buffs;
    for (const auto& trace:report.buffs)
        if (trace.current.level)
            buffs.append(&trace);
    const auto& style=report.appearance; auto colors=palette(style.theme);
    const bool nectar_visible=style.nectar && !report.nectar.empty();
    const int summary_height=style.activity?132:70;
    const int nectar_y=50+summary_height+12;
    const int chart_y=nectar_y+(nectar_visible?74:0),buffs_y=chart_y+144;
    const auto rows=buff_rows(buffs,buffs_y+20,style.chart_height);
    const double plot_left=std::any_of(buffs.begin(),buffs.end(),[](const auto* trace) { return trace->current.icon.isNull(); })?144:92;
    QList<const Section*> panels;
    for (const auto& section:snapshot.sections)
        if (style.modules && std::any_of(section.metrics.begin(),section.metrics.end(),[](const auto& metric) { return metric.progress.has_value(); })) panels.append(&section);
    int panel_height=0;
    for (const auto* section:panels.first(std::min(qsizetype(4),panels.size()))) {
        int rows=0; for (const auto& metric:section->metrics) if (metric.progress) ++rows;
        panel_height+=(panel_height?12:0)+34+std::min(6,rows)*43;
    }
    const int chart_bottom=rows.empty()?buffs_y-12:int(rows.back().top+rows.back().height-(rows.back().ceiling==1?8:18));
    const int buff_height=chart_bottom-buffs_y+36;
    const int details_y=buffs_y+buff_height+12;
    QList<CounterResult> counts;
    for (const auto& section:snapshot.sections) for (auto count:summarize(report,section)) {
        count.label=section.name+(count.label=="Completed"?QString{}:" · "+count.label.toLower()); counts.append(count);
    }
    for (const auto& count:report.counts.value({})) if (count.session || count.period) counts.append(count);
    const int shown=style.totals?std::min(10,int(counts.size())):0;
    const int stats_height=shown?38+shown*22:0;
    const int bottom=details_y+std::max(panel_height,stats_height);
    const bool partial=report.tracked_ms+30000<report.session_ms;
    QStringList notes;
    if (report.alerts) notes.append(QString("%1 alerts").arg(report.alerts));
    if (partial) notes.append("Tracked "+duration(report.tracked_ms)+" of this session");
    else if (report.honey.baseline_delay>30000) notes.append("Honey starts "+duration(report.honey.baseline_delay)+" into the session");
    if (std::any_of(buffs.begin(),buffs.end(),[&](const auto* trace) { return report.window_ms && trace->observed_ms<report.window_ms*9/10; }))
        notes.append("* Uptime uses readable samples; gaps are unknown");
    if (panels.size()>4 || (style.totals && counts.size()>shown)) notes.append("Additional activity omitted");
    QImage image(width,bottom+18+(notes.empty()?0:20),QImage::Format_RGB32); image.fill(colors.background);
    QPainter p(&image); p.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing|QPainter::SmoothPixmapTransform);
    const auto uptime=[&](const GaugeHistory& trace) {
        const bool incomplete=report.window_ms && trace.observed_ms<report.window_ms*9/10;
        return trace.observed_ms?QString::number(100.*trace.active_ms/trace.observed_ms,'f',0)+"%"+(incomplete?"*":""):QString("—");
    };
    const auto rate_scale=honey_scale(report);
    double value_width=38;
    const auto fit_value=[&](const QString& label,int size,bool bold=false) {
        font(p,size,bold);
        value_width=std::max(value_width,double(p.fontMetrics().horizontalAdvance(label)+2));
    };
    if (rate_scale.observed)
        for (int i=0;i<3;++i) fit_value(amount(rate_scale.high-i*(rate_scale.high-rate_scale.low)/2),11);
    for (const auto& row:rows) {
        const bool grouped=row.traces.size()>1;
        for (const auto* trace:row.traces)
            fit_value((grouped?trace->current.name.left(1)+" ":QString{})+uptime(*trace),grouped?10:12,true);
    }
    const double value_left=960-value_width,plot_width=value_left-6-plot_left;
    if (!style.background_image.isEmpty()) {
        const auto background=cached_background?*cached_background:load_report_background(style.background_image);
        if (!background.isNull()) {
            const auto crop=report_background_crop(background.size(),image.size(),style);
            const auto scale=image.width()/crop.width();
            // Below 100%, leave the theme visible around the smaller image.
            p.drawImage(QRectF(-crop.x()*scale,-crop.y()*scale,background.width()*scale,background.height()*scale),background);
            auto shade=colors.background; shade.setAlpha(std::clamp(style.background_fade,0,100)*255/100); p.fillRect(image.rect(),shade);
            colors.panel.setAlpha(175+std::clamp(style.background_fade,0,100)*80/100);
        }
    }
    p.fillRect(0,0,width,4,colors.heading);
    text(p,{margin,13,160,28},"AUREX",22,colors.heading,true);
    const auto period=end.addMSecs(-report.window_ms).toString("d MMM · HH:mm")+" – "+end.toString("HH:mm");
    text(p,{195,15,420,24},period,14,colors.muted);
    text(p,{756,15,220,24},"Session "+duration(report.session_ms),14,colors.muted,false,Qt::AlignRight|Qt::AlignVCenter);
    const auto money=[](auto value) { return value?amount(double(*value)):QString("—"); };
    const auto gain=[&](auto value) { return value && *value>0?"+"+money(value):money(value); };
    card(p,{margin,50,content,double(summary_height)},colors);
    const QStringList labels{"Hourly average","Current honey","Honey · "+duration(report.window_ms),partial?"Tracked session gain":"Session gain"};
    const QStringList values{money(report.honey.hourly),money(report.honey.current),gain(report.honey.period),gain(report.honey.session)};
    for (int i=0;i<4;++i) {
        const double x=margin+18+(style.activity?i%2*255:i*content/4.);
        const double y=style.activity?57+i/2*60:57;
        text(p,{x,y,230,19},labels[i],12,colors.muted);
        text(p,{x,y+21,230,30},values[i],26,i==0?accent(1,colors):i==2?accent(report.honey.period && *report.honey.period<0?4:0,colors):colors.foreground,true);
    }
    if (style.activity) {
        p.setPen(colors.divider); p.drawLine(QPointF(556,66),QPointF(556,166));
        activity_pie(p,{576,59,380,111},report,colors);
    }
    if (nectar_visible) nectar_summary(p,{margin,double(nectar_y),content,62},report.nectar,colors);
    card(p,{margin,double(chart_y),content,double(buffs_y+buff_height-chart_y)},colors);
    for (int i=0;i<2;++i) {
        const double x=40+i*122;
        if (i) {
            p.fillRect(QRectF(x,chart_y+16,3,8),accent(i,colors)); p.fillRect(QRectF(x+5,chart_y+12,3,12),accent(i,colors)); p.fillRect(QRectF(x+10,chart_y+18,3,6),accent(i,colors));
        } else { p.setPen(QPen(accent(i,colors),2)); p.drawLine(QPointF(x,chart_y+20),QPointF(x+16,chart_y+20)); }
        text(p,{x+23,double(chart_y+9),130,22},i?"Honey / sec":"Backpack",12,accent(i,colors));
    }
    honey_backpack_chart(p,{plot_left,double(chart_y+42),plot_width,90},report,rate_scale,colors);
    if (!rows.empty()) {
        p.setPen(colors.divider); p.drawLine(QPointF(40,buffs_y),QPointF(960,buffs_y));
        text(p,{value_left,double(buffs_y+2),value_width,18},"Uptime",10,colors.subtle,false,Qt::AlignRight|Qt::AlignVCenter);
    }
    for (qsizetype i=0;i<rows.size();++i) {
        const auto& row=rows[i];
        const bool binary=row.ceiling==1,grouped=row.traces.size()>1;
        const double y=row.top,height=row.height-(binary?8:18);
        const double band_height=binary?18.*std::clamp(style.chart_height,75,150)/100.:height;
        const QRectF plot(plot_left,y+(height-band_height)/2,plot_width,band_height);
        const double high=row.ceiling;
        for (int grid=0;grid<3;++grid) {
            if (binary && grid==1) continue;
            const auto gy=plot.top()+grid*plot.height()/2;
            p.setPen(QPen((grid==1?colors.grid_secondary:colors.grid),1));
            p.drawLine(QPointF(plot.left(),gy),QPointF(plot.right(),gy));
        }
        for (int tick=1;tick<4;++tick) {
            const auto x=plot.left()+plot.width()*tick/4;
            p.setPen(QPen(colors.grid_vertical,1,Qt::DashLine)); p.drawLine(QPointF(x,plot.top()),QPointF(x,plot.bottom()));
        }
        bool observed=false;
        for (qsizetype j=0;j<row.traces.size();++j) {
            const auto& trace=*row.traces[j];
            const auto color=trace.current.color.isValid()?colors.ink(trace.current.color):accent(i+j,colors);
            if (binary) activity_strip(p,plot,trace.samples,report.window_ms,color);
            else plot_line(p,plot,trace.samples,report.window_ms,0,high,color,&Sample::level);
            observed|=trace.peak.has_value();
            const auto center=y+height/2,label_height=std::min(36.,height);
            if (trace.current.icon.isNull()) {
                text(p,{36,grouped?y+14*j:center-label_height/2,double(plot_left-44),grouped?14.:label_height},trace.current.name,11,color,true);
            } else {
                const auto size=trace.current.icon.size().scaled(36,int(label_height),Qt::KeepAspectRatio);
                p.save();
                if (grouped) p.setClipRect(QRectF(42+36.*j/row.traces.size(),center-18,36./row.traces.size(),36));
                p.drawImage(QRectF(42+(36-size.width())/2.,center-size.height()/2.,size.width(),size.height()),trace.current.icon);
                p.restore();
            }
            text(p,{value_left,grouped?center-7*row.traces.size()+14*j:center-18,value_width,grouped?14.:36.},
                (grouped?trace.current.name.left(1)+" ":QString{})+uptime(trace),grouped?10:12,
                grouped?color:colors.muted,true,Qt::AlignRight|Qt::AlignVCenter);
        }
        const auto scale_color=!grouped && row.traces.front()->current.color.isValid()?colors.ink(row.traces.front()->current.color):colors.muted;
        if (!binary) text(p,{36,y+height+1,plot_left-44,13},"×"+QString::number(high,'g',3),10,scale_color,false,Qt::AlignHCenter|Qt::AlignVCenter);
        if (!observed) text(p,plot,"Waiting for readings",11,colors.subtle,false,Qt::AlignCenter);
    }
    time_axis(p,{plot_left,double(chart_bottom)-26,plot_width,26},report.window_ms,end,colors);
    const int columns=(!panels.empty()?1:0)+(shown?1:0);
    const double detail_width=columns?(content-12.*(columns-1))/columns:content;
    const double panel_x=margin;
    const double totals_x=panel_x+(!panels.empty()?detail_width+12:0);
    int side_y=details_y;
    for (const auto* section:panels.first(std::min(qsizetype(4),panels.size()))) {
        QList<Metric> metrics; for (const auto& metric:section->metrics) if (metric.progress && metrics.size()<6) metrics.append(metric);
        const int height=34+metrics.size()*43;
        card(p,{panel_x,double(side_y),detail_width,double(height)},colors);
        text(p,{panel_x+16,double(side_y+7),detail_width-32,24},section->name,15,colors.foreground,true);
        for (int i=0;i<metrics.size();++i) {
            const auto& metric=metrics[i]; const double y=side_y+35+i*43;
            text(p,{panel_x+16,y,106,20},metric.label,13,colors.muted);
            text(p,{panel_x+128,y,detail_width-144,20},metric.value,13,colors.foreground,true,Qt::AlignRight|Qt::AlignVCenter);
            bar(p,{panel_x+16,y+26,detail_width-32,4},metric.progress,metric.color.isValid()?colors.ink(metric.color):accent(i,colors),colors);
        }
        side_y+=height+12;
    }
    if (shown) {
        card(p,{totals_x,double(details_y),detail_width,double(stats_height)},colors);
        text(p,{totals_x+16,double(details_y+7),detail_width-164,22},"Completed",15,colors.foreground,true);
        text(p,{totals_x+detail_width-139,double(details_y+9),58,20},duration(report.window_ms),11,colors.subtle,false,Qt::AlignRight|Qt::AlignVCenter);
        text(p,{totals_x+detail_width-74,double(details_y+9),58,20},"Session",11,colors.subtle,false,Qt::AlignRight|Qt::AlignVCenter);
        for (int i=0;i<shown;++i) {
            const auto& count=counts[i]; const double y=details_y+32+i*22;
            text(p,{totals_x+16,y,detail_width-162,20},count.label,12,colors.muted);
            text(p,{totals_x+detail_width-139,y,58,20},amount(double(count.period)),13,colors.foreground,true,Qt::AlignRight|Qt::AlignVCenter);
            text(p,{totals_x+detail_width-74,y,58,20},amount(double(count.session)),12,colors.subtle,false,Qt::AlignRight|Qt::AlignVCenter);
        }
    }
    if (!notes.empty()) text(p,{margin,double(bottom+3),content,20},notes.join("   ·   "),11,colors.subtle);
    return image;
}
}
