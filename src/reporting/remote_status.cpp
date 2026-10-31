#include "remote.hpp"
#include <algorithm>
#include <cmath>
#include <QPainter>

namespace hive::reporting {
QImage remote_cards(const QString& title,const QList<RemoteCard>& cards) {
    if (cards.isEmpty()) return {};
    QImage image(640,64+cards.size()*84,QImage::Format_ARGB32_Premultiplied); image.fill(QColor("#14191f"));
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
    QFont font; font.setPixelSize(21); font.setBold(true); p.setFont(font); p.setPen(QColor("#f3f5f7"));
    p.drawText(QRect(22,14,596,36),Qt::AlignVCenter,title);
    int y=64;
    for (const auto& card:cards) {
        const auto color=card.color.isValid()?card.color:QColor("#efc156");
        p.setPen(Qt::NoPen); p.setBrush(QColor("#20272f")); p.drawRoundedRect(QRectF(16,y,608,72),8,8);
        p.setBrush(color); p.drawRoundedRect(QRectF(16,y+12,3,48),1,1);
        font.setPixelSize(16); font.setBold(true); p.setFont(font); p.setPen(QColor("#eef1f5"));
        p.drawText(QRect(30,y+5,574,24),Qt::AlignVCenter,p.fontMetrics().elidedText(card.label,Qt::ElideRight,574));
        font.setPixelSize(14); font.setBold(false); p.setFont(font); p.setPen(QColor("#bac5d1"));
        p.drawText(QRect(30,y+27,574,24),Qt::AlignVCenter,p.fontMetrics().elidedText(QString(card.value).replace('\n'," · "),Qt::ElideRight,574));
        if (card.percent && std::isfinite(*card.percent)) {
            const QRectF bar(30,y+58,574,5); p.setPen(Qt::NoPen); p.setBrush(QColor("#39434f")); p.drawRoundedRect(bar,2,2);
            p.setBrush(color); p.drawRoundedRect(QRectF(bar.x(),bar.y(),bar.width()*std::clamp(*card.percent,0.,100.)/100.,bar.height()),2,2);
            if (card.floor && std::isfinite(*card.floor)) { const auto x=bar.x()+bar.width()*std::clamp(*card.floor,0.,100.)/100.;p.setPen(QPen(QColor("#ffffff"),2));p.drawLine(QPointF(x,bar.y()-3),QPointF(x,bar.bottom()+3)); }
        }
        y+=84;
    }
    return image;
}
RemoteReply status_reply(const Report& report) {
    const auto& snapshot=report.snapshot;
    RemoteReply reply{"Status",snapshot.state};
    reply.inline_fields=true;
    if (!snapshot.activity.isEmpty()) reply.description+="\n"+snapshot.activity;
    const auto money=[](auto value) { return value?amount(double(*value)):QString("Unavailable"); };
    reply.fields={{"Session",duration(report.session_ms)},{report.honey.current_age_ms>2*sample_interval_ms?"Last known honey":"Honey",money(report.honey.current)},
        {"Honey/hour",money(report.honey.hourly)},{"Session honey",money(report.honey.session)},
        {"Backpack",snapshot.trend.percent?QString::number(std::clamp(*snapshot.trend.percent,0.,100.),'f',0)+"%":QString("Unavailable")}};
    QStringList nectars;
    for (const auto& gauge:snapshot.gauges)
        if (gauge.percent && std::isfinite(*gauge.percent)) nectars.append(gauge.name+(gauge.estimated?" ~":" ")+QString::number(std::clamp(*gauge.percent,0.,100.),'f',0)+"%");
    if (!nectars.isEmpty()) reply.fields.append({"Nectar",nectars.join(" · ")});
    return reply;
}
RemoteReply nectar_reply(const Report& report,std::optional<qint64> age_ms) {
    RemoteReply reply{"Nectar",age_ms?"Read "+duration(*age_ms)+" ago.":"No recent nectar reading available."};
    QList<RemoteCard> cards;
    for(const auto& gauge:report.snapshot.gauges) {
        QString value="Unknown";
        if(age_ms && gauge.percent && std::isfinite(*gauge.percent)) {
            const auto percent=std::clamp(*gauge.percent,0.,100.);
            value=QString::number(percent,'f',1)+"% · "+duration(qint64(percent*24*60*60*10))+" remaining";
        }
        if(gauge.floor)value+="\nMinimum: "+QString::number(*gauge.floor,'f',0)+"%";
        cards.append({gauge.name,value,age_ms?gauge.percent:std::nullopt,gauge.color,gauge.floor});
    }
    reply.image=remote_cards(reply.title,cards);
    return reply;
}
}
