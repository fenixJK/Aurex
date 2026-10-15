#include "module_plot.hpp"
#include <QHeaderView>
#include <QGridLayout>
#include <QTabBar>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QSaveFile>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>
#include <algorithm>

namespace hive {
namespace {
QString text(const std::string& value) { return QString::fromStdString(value); }
void validate_plot(const extensions::Plot& plot) {
    if (plot.title.size()>120 || plot.description.size()>400) throw std::invalid_argument("Module chart text is too long");
    if (!std::isfinite(plot.x_max) || plot.x_max<=0 || !std::isfinite(plot.y_max) || plot.y_max<=0
        || plot.series.size()>12 || plot.statistics.size()>16 || plot.tables.size()>4) throw std::invalid_argument("Invalid module chart");
    for (const auto& table:plot.tables) {
        if(table.columns.empty() || table.columns.size()>6 || table.rows.size()>64 || table.title.size()>80 || table.note.size()>400)
            throw std::invalid_argument("Invalid module chart table");
        for(const auto& row:table.rows) {
            if(row.size()!=table.columns.size()) throw std::invalid_argument("Invalid module chart row");
            for(const auto& value:row) if(value.size()>240) throw std::invalid_argument("Module chart cell is too long");
        }
    }
    for (const auto& series:plot.series) {
        if (series.label.size()>80 || series.points.size()>20000) throw std::invalid_argument("Module chart has too many points");
        double last=-1;
        for (const auto p:series.points) {
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x<last || p.x<0 || p.x>plot.x_max || p.y<0 || p.y>plot.y_max)
                throw std::invalid_argument("Invalid module chart point");
            last=p.x;
        }
    }
}
QColor series_color(const extensions::Plot& plot,std::size_t i) {
    const QColor colors[]{"#7bc9ed","#83cf9d","#f0c56b","#d699dd","#f09882","#b3b5ed"};
    const QColor chosen(text(plot.series[i].color)); return chosen.isValid()?chosen:colors[i%6];
}
void paint_chart(QPainter& p,QRectF area,const extensions::Plot& plot) {
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF chart=area.adjusted(42,22,-14,-28);
    p.setPen(QColor("#a5b5c4")); p.drawText(area.adjusted(42,0,0,0),Qt::AlignTop|Qt::AlignLeft,text(plot.y_label));
    for(int i=0;i<=4;++i) {
        const double y=chart.bottom()-chart.height()*i/4.;p.setPen(QColor("#34424c"));p.drawLine(QPointF(chart.left(),y),QPointF(chart.right(),y));
        p.setPen(QColor("#a5b5c4"));p.drawText(QRectF(area.left(),y-8,34,16),Qt::AlignRight,QString::number(plot.y_max*i/4.,'g',3));
    }
    p.save();p.setClipRect(chart);
    for(std::size_t i=0;i<plot.series.size();++i) {
        QPainterPath path;bool first=true;
        for(const auto point:plot.series[i].points) {
            const QPointF at(chart.left()+point.x/plot.x_max*chart.width(),chart.bottom()-point.y/plot.y_max*chart.height());
            if(first)path.moveTo(at);else path.lineTo(at);first=false;
        }
        p.setPen(QPen(series_color(plot,i),1.5));p.drawPath(path);
    }
    p.restore();p.setPen(QColor("#a5b5c4"));
    for(int i=0;i<=4;++i)p.drawText(QRectF(chart.left()+chart.width()*i/4.-30,chart.bottom()+5,60,16),Qt::AlignCenter,QString::number(plot.x_max*i/4.,'g',3));
    p.drawText(area.adjusted(42,0,-14,0),Qt::AlignTop|Qt::AlignRight,text(plot.x_label));
}
class Chart final:public QWidget {
    std::shared_ptr<const extensions::Plot> plot_;
    void paintEvent(QPaintEvent*) override {QPainter p(this);paint_chart(p,rect(),*plot_);}
public:
    explicit Chart(std::shared_ptr<const extensions::Plot> plot):plot_(std::move(plot)) {setMinimumHeight(190);}
};
QLabel* label(const QString& value,const char* name) {
    auto result=new QLabel(value);result->setObjectName(name);result->setWordWrap(true);return result;
}
QTableWidget* table_widget(const extensions::PlotTable& data) {
    auto table=new QTableWidget(int(data.rows.size()),int(data.columns.size()));
    QStringList columns;for(const auto& column:data.columns)columns<<text(column);table->setHorizontalHeaderLabels(columns);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);table->setShowGrid(false);table->setAlternatingRowColors(true);
    table->setStyleSheet("QTableWidget {background:#111b20; alternate-background-color:#1a282e; color:#dce5ed; border:none;} QTableWidget::item {padding:2px 4px;} QHeaderView::section {padding:4px;}");
    table->horizontalHeader()->setFixedHeight(28);table->verticalHeader()->setMinimumSectionSize(25);
    table->setFrameShape(QFrame::NoFrame);table->verticalHeader()->hide();table->verticalHeader()->setDefaultSectionSize(25);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);
    table->horizontalHeader()->setMinimumSectionSize(92);table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft|Qt::AlignVCenter);
    for(std::size_t r=0;r<data.rows.size();++r)for(std::size_t c=0;c<data.columns.size();++c) {
        auto item=new QTableWidgetItem(text(data.rows[r][c]));table->setItem(int(r),int(c),item);
    }
    if(data.with_chart) table->setFixedHeight(30+25*int(data.rows.size()));
    return table;
}
void clear_layout(QLayout* layout) {
    while(auto item=layout->takeAt(0)) {delete item->widget();if(auto child=item->layout())clear_layout(child);delete item;}
}
}
ModulePlot::ModulePlot(QWidget* parent):QWidget(parent) {
    setObjectName("module-plot");auto body=new QVBoxLayout(this);body->setContentsMargins(0,0,0,0);body->setSpacing(8);
    body->addWidget(label("Run a simulation to see the forecast","muted"),1,Qt::AlignCenter);
}
void ModulePlot::setPlot(std::shared_ptr<const extensions::Plot> plot) {
    if(plot==plot_)return;
    if(plot)validate_plot(*plot);plot_=std::move(plot);clear_layout(layout());
    auto body=static_cast<QVBoxLayout*>(layout());
    if(!plot_) {body->addWidget(label("Run a simulation to see the forecast","muted"),1,Qt::AlignCenter);return;}
    body->addWidget(label(text(plot_->description),"muted"));
    auto cards=new QGridLayout;cards->setSpacing(8);
    for(std::size_t i=0;i<plot_->statistics.size();++i) {
        const auto& [name,value]=plot_->statistics[i];auto card=new QFrame;card->setObjectName("card");
        auto stack=new QVBoxLayout(card);stack->setContentsMargins(10,6,10,6);stack->setSpacing(2);
        stack->addWidget(label(text(value),"section-title"));stack->addWidget(label(text(name),"muted"));cards->addWidget(card,int(i/3),int(i%3));
    }
    if(!plot_->statistics.empty())body->addLayout(cards);else delete cards;
    auto tabs=new QTabWidget;body->addWidget(tabs,1);
    auto page=new QWidget;auto chart_body=new QVBoxLayout(page);chart_body->setContentsMargins(4,4,4,4);chart_body->setSpacing(4);
    chart_body->addWidget(new Chart(plot_),1);
    QString chart_title="Chart";
    for(const auto& data:plot_->tables) {
        QVBoxLayout* target=chart_body;
        if(data.with_chart)chart_title=text(data.title);
        else {auto table_page=new QWidget;target=new QVBoxLayout(table_page);target->setContentsMargins(8,8,8,8);tabs->addTab(table_page,text(data.title));}
        auto table=table_widget(data);
        if(data.with_chart)for(int i=0;i<table->rowCount() && i<int(plot_->series.size());++i)table->item(i,0)->setForeground(series_color(*plot_,std::size_t(i)));
        target->addWidget(table,data.with_chart?0:1);
        if(!data.note.empty())target->addWidget(label(text(data.note),"muted"));
    }
    if(std::none_of(plot_->tables.begin(),plot_->tables.end(),[](const auto& table){return table.with_chart;})) {
        QString legend;for(std::size_t i=0;i<plot_->series.size();++i)legend+="<span style='color:"+series_color(*plot_,i).name()+"'>"+text(plot_->series[i].label).toHtmlEscaped()+"</span> &nbsp; ";
        chart_body->addWidget(label(legend,"muted"));
    }
    tabs->insertTab(0,page,chart_title);tabs->setCurrentIndex(0);tabs->tabBar()->setVisible(tabs->count()>1);
}
QImage plot_image(const extensions::Plot& plot,QSize size) {
    validate_plot(plot);size.setWidth(std::max(640,size.width()));
    const int cards=plot.statistics.empty()?0:((int(plot.statistics.size())+2)/3)*56;
    int required=90+cards+320;
    for(const auto& table:plot.tables)required+=56+26*int(table.rows.size())+(table.note.empty()?0:40);
    size.setHeight(std::max(size.height(),required));
    QImage image(size,QImage::Format_ARGB32_Premultiplied);image.fill(QColor("#111820"));QPainter p(&image);
    auto font=p.font();font.setPointSize(12);font.setBold(true);p.setFont(font);p.setPen(QColor("#dce5ed"));p.drawText(QRectF(20,12,size.width()-40,26),text(plot.title));
    font.setPointSize(9);font.setBold(false);p.setFont(font);p.setPen(QColor("#a5b5c4"));p.drawText(QRectF(20,42,size.width()-40,40),Qt::TextWordWrap,text(plot.description));
    const double card_width=(size.width()-56)/3.;int y=88;
    for(std::size_t i=0;i<plot.statistics.size();++i) {
        const auto& [name,value]=plot.statistics[i];const QRectF card(20+(i%3)*(card_width+8),y+(i/3)*56,card_width,48);
        p.fillRect(card,QColor("#1b262e"));p.setPen(QColor("#dce5ed"));p.drawText(card.adjusted(10,4,-10,-20),text(value));
        p.setPen(QColor("#a5b5c4"));p.drawText(card.adjusted(10,26,-10,0),text(name));
    }
    y+=cards;paint_chart(p,QRectF(10,y,size.width()-20,290),plot);y+=296;
    if(std::none_of(plot.tables.begin(),plot.tables.end(),[](const auto& table){return table.with_chart;})) {
        double x=20;for(std::size_t i=0;i<plot.series.size();++i) {p.setPen(series_color(plot,i));p.drawText(QPointF(x,y+12),text(plot.series[i].label));x+=p.fontMetrics().horizontalAdvance(text(plot.series[i].label))+20;}y+=24;
    }
    for(const auto& table:plot.tables) {
        p.setPen(QColor("#dce5ed"));font.setBold(true);p.setFont(font);p.drawText(QRectF(20,y,size.width()-40,24),text(table.title));y+=28;
        const double first=(size.width()-40)*.36,other=(size.width()-40-first)/std::max<std::size_t>(1,table.columns.size()-1);
        const auto draw_row=[&](const auto& row,int at,bool heading,int index) {
            if(heading || index%2==0)p.fillRect(QRectF(20,at,size.width()-40,26),QColor(heading?"#24313a":"#19232b"));
            double x=28;for(std::size_t c=0;c<row.size();++c) {
                const auto width=c?other:first;
                p.setPen(table.with_chart && !heading && c==0 && std::size_t(index)<plot.series.size()?series_color(plot,index):QColor(heading?"#a5b5c4":"#dce5ed"));
                p.drawText(QRectF(x,at+4,width-16,20),p.fontMetrics().elidedText(text(row[c]),Qt::ElideRight,int(width-16)));x+=width;
            }
        };
        draw_row(table.columns,y,true,0);y+=26;font.setBold(false);p.setFont(font);
        for(std::size_t i=0;i<table.rows.size();++i){draw_row(table.rows[i],y,false,int(i));y+=26;}
        if(!table.note.empty()){p.setPen(QColor("#a5b5c4"));p.drawText(QRectF(20,y+6,size.width()-40,34),Qt::TextWordWrap,text(table.note));y+=40;}
        y+=4;
    }
    return image;
}
bool ModulePlot::savePng(const QString& path) {return plot_ && plot_image(*plot_).save(path,"PNG");}
bool ModulePlot::saveCsv(const QString& path) {
    if(!plot_)return false;QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return false;
    QTextStream out(&file);out<<"section,label,time,metric,value\n";
    const auto row=[&](const QStringList& cells) {
        QStringList escaped;for(auto cell:cells) {cell.replace('"',"\"\"");escaped<<'"'+cell+'"';}out<<escaped.join(',')<<'\n';
    };
    for(const auto& series:plot_->series)for(const auto point:series.points)
        row({"Chart",text(series.label),QString::number(point.x,'g',12),text(plot_->y_label),QString::number(point.y,'g',12)});
    for(const auto& [name,value]:plot_->statistics)row({"Summary",text(name),{},text(name),text(value)});
    for(const auto& table:plot_->tables)for(const auto& values:table.rows)for(std::size_t i=1;i<values.size();++i)
        row({text(table.title),text(values.front()),{},text(table.columns[i]),text(values[i])});
    out.flush();return out.status()==QTextStream::Ok && file.commit();
}
}
