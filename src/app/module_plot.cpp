#include "module_plot.hpp"
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QTextStream>
#include <algorithm>
namespace hive {
namespace {
void validate_plot(const extensions::Plot* plot) {
    if (plot) {
        if (plot->title.size()>120 || plot->description.size()>400) throw std::invalid_argument("Module chart text is too long");
        if (!std::isfinite(plot->x_max) || plot->x_max<=0 || !std::isfinite(plot->y_max) || plot->y_max<=0
            || plot->series.size()>12 || plot->statistics.size()>16) throw std::invalid_argument("Invalid module chart");
        for (const auto& series:plot->series) {
            if (series.label.size()>80 || series.points.size()>20000) throw std::invalid_argument("Module chart has too many points");
            double last=-1;
            for (const auto p:series.points) {
                if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x<last || p.x<0 || p.x>plot->x_max || p.y<0 || p.y>plot->y_max)
                    throw std::invalid_argument("Invalid module chart point");
                last=p.x;
            }
        }
    }
}
void paint_plot(QPainter& p,QSize size,const extensions::Plot* plot_) {
    const auto width=[&] { return size.width(); }; const auto height=[&] { return size.height(); };
    const QRect rect(QPoint{},size);
    p.setRenderHint(QPainter::Antialiasing); p.fillRect(rect,QColor("#111820"));
    p.setPen(QColor("#dce5ed"));
    if (!plot_) { p.drawText(rect,Qt::AlignCenter,"Run a simulation to see the forecast"); return; }
    const auto text=[](const std::string& s) { return QString::fromStdString(s); };
    auto font=p.font(); font.setPointSize(12); font.setBold(true); p.setFont(font);
    p.drawText(QRectF(18,10,width()-36,25),text(plot_->title));
    font.setPointSize(9);font.setBold(false);p.setFont(font); p.setPen(QColor("#a5b5c4"));
    p.drawText(QRectF(18,36,width()-36,35),Qt::TextWordWrap,text(plot_->description));
    const QRectF chart(46,85,width()-66,std::max(100.,height()-200.-plot_->statistics.size()*18.));
    for (int i=0;i<=4;++i) {
        const double y=chart.bottom()-chart.height()*i/4.; p.setPen(QColor("#28323c"));p.drawLine(QPointF(chart.left(),y),QPointF(chart.right(),y));
        p.setPen(QColor("#a5b5c4"));p.drawText(QRectF(0,y-8,40,16),Qt::AlignRight,QString::number(plot_->y_max*i/4.,'g',3));
    }
    const QColor colors[]{"#7bc9ed","#83cf9d","#f0c56b","#d699dd","#f09882","#b3b5ed"};
    const auto color=[&](std::size_t i) { const QColor chosen(QString::fromStdString(plot_->series[i].color)); return chosen.isValid()?chosen:colors[i%6]; };
    p.save();p.setClipRect(chart);
    for (std::size_t i=0;i<plot_->series.size();++i) {
        QPainterPath path; bool first=true;
        for (const auto point:plot_->series[i].points) {
            const QPointF at(chart.left()+point.x/plot_->x_max*chart.width(),chart.bottom()-point.y/plot_->y_max*chart.height());
            if(first) path.moveTo(at);else path.lineTo(at); first=false;
        }
        p.setPen(QPen(color(i),1.6));p.drawPath(path);
    }
    p.restore(); p.setPen(QColor("#a5b5c4"));
    for(int i=0;i<=4;++i) p.drawText(QRectF(chart.left()+chart.width()*i/4.-35,chart.bottom()+5,70,18),Qt::AlignCenter,QString::number(plot_->x_max*i/4.,'g',3));
    p.drawText(QRectF(chart.left(),chart.bottom()+23,chart.width(),18),Qt::AlignCenter,text(plot_->x_label));
    double x=18;const double y=chart.bottom()+45;
    for(std::size_t i=0;i<plot_->series.size();++i) {
        p.setPen(color(i));p.drawText(QPointF(x,y+12),text(plot_->series[i].label));x+=p.fontMetrics().horizontalAdvance(text(plot_->series[i].label))+20;
    }
    p.setPen(QColor("#dce5ed")); double row=y+20;
    for (const auto& [label,value]:plot_->statistics) {
        p.drawText(QRectF(18,row,width()-36,18),p.fontMetrics().elidedText(text(label)+": "+text(value),Qt::ElideRight,width()-36)); row+=18;
    }
}
}
void ModulePlot::setPlot(std::shared_ptr<const extensions::Plot> plot) {
    if (plot==plot_) return;
    validate_plot(plot.get()); plot_=std::move(plot); update();
}
void ModulePlot::paintEvent(QPaintEvent*) { QPainter painter(this); paint_plot(painter,size(),plot_.get()); }
QImage plot_image(const extensions::Plot& plot,QSize size) {
    validate_plot(&plot);
    QImage image(size,QImage::Format_ARGB32_Premultiplied); QPainter painter(&image);
    paint_plot(painter,size,&plot); return image;
}
bool ModulePlot::savePng(const QString& path) {
    if(!plot_)return false; const auto old=size(); resize(std::max(width(),1000),480); const auto image=grab(); resize(old); return image.save(path,"PNG");
}
bool ModulePlot::saveCsv(const QString& path) {
    if(!plot_)return false;QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return false;
    QTextStream out(&file);out<<"series,time,value\n";
    for(const auto& series:plot_->series) { auto name=QString::fromStdString(series.label);name.replace('"',"\"\"");
        for(const auto point:series.points) out<<'"'<<name<<"\","<<QString::number(point.x,'g',12)<<','<<QString::number(point.y,'g',12)<<'\n';
    }
    out.flush();return out.status()==QTextStream::Ok && file.commit();
}
}
