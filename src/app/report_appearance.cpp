#include "report_appearance.hpp"
#include <QDialog>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QScrollArea>
#include <QFileDialog>
#include <QImageReader>
#include <QSaveFile>
#include <QDir>
#include <QTimer>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>
#include <memory>

namespace hive::app {
namespace {
class ReportPreview final : public QLabel {
    reporting::ReportAppearance& appearance_;
    QImage report_;
    QPointF last_;
    bool dragging_{},moved_{};
    void rescale() {
        if (report_.isNull() || width()<1) return;
        const auto ratio=devicePixelRatioF();
        auto pixmap=QPixmap::fromImage(report_.scaledToWidth(std::lround(width()*ratio),Qt::SmoothTransformation));
        pixmap.setDevicePixelRatio(ratio);setPixmap(pixmap);
        const auto height=int(std::ceil(pixmap.deviceIndependentSize().height()));
        if (height!=minimumHeight()) { setMinimumHeight(height);if (size_changed) size_changed(); }
    }
protected:
    void resizeEvent(QResizeEvent* event) override { QLabel::resizeEvent(event);rescale(); }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button()!=Qt::LeftButton || background.isNull() || report_.isNull()
            || event->position().y()>pixmap().deviceIndependentSize().height()) { QLabel::mousePressEvent(event);return; }
        dragging_=true;moved_=false;last_=event->position();setCursor(Qt::ClosedHandCursor);event->accept();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (!dragging_) { QLabel::mouseMoveEvent(event);return; }
        const auto crop=reporting::report_background_crop(background.size(),report_.size(),appearance_);
        const auto shown=pixmap().deviceIndependentSize();const auto delta=event->position()-last_;last_=event->position();
        const auto pan=[](double value,double delta,double crop,double shown,double overflow) {
            return std::abs(overflow)>0.001 && shown>0?std::clamp(value-delta*crop/shown/overflow,0.,1.):value;
        };
        const auto x=pan(appearance_.background_x,delta.x(),crop.width(),shown.width(),background.width()-crop.width());
        const auto y=pan(appearance_.background_y,delta.y(),crop.height(),shown.height(),background.height()-crop.height());
        if (x!=appearance_.background_x || y!=appearance_.background_y) {
            appearance_.background_x=x;appearance_.background_y=y;moved_=true;if (changed) changed();
        }
        event->accept();
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button()!=Qt::LeftButton || !dragging_) { QLabel::mouseReleaseEvent(event);return; }
        dragging_=false;setCursor(Qt::OpenHandCursor);if (moved_ && finished) finished();event->accept();
    }
public:
    QImage background;
    std::function<void()> changed,finished,size_changed;
    explicit ReportPreview(reporting::ReportAppearance& appearance):appearance_(appearance) {
        setAlignment(Qt::AlignTop|Qt::AlignHCenter);setMinimumWidth(1);
    }
    void setReport(QImage report) {
        report_=std::move(report);rescale();
        setCursor(background.isNull()?Qt::ArrowCursor:dragging_?Qt::ClosedHandCursor:Qt::OpenHandCursor);
    }
};
}
reporting::Report example_report(const reporting::ReportAppearance& appearance,const QList<reporting::Gauge>& buffs) {
    reporting::Report report;report.appearance=appearance;
    report.created=QDateTime(QDate(2026,1,1),QTime(13,0));
    report.window_ms=3600000;report.session_ms=report.tracked_ms=7200000;
    report.honey.current=180000000000ULL;report.honey.period=12000000000LL;report.honey.session=24500000000LL;report.honey.hourly=12250000000.;
    report.activities={{"Gathering",3000000},{"Converting",360000},{"Travelling",240000}};
    for(int i=0;i<=360;++i){
        report.trend.append({i*10000LL,10.+std::fmod(i*2.7,85.)});
        report.honey.rates.append({i*10000LL,3000000.+1500000.*std::sin(i*.06)});
    }
    for(const auto& name:{"Comforting","Satisfying","Refreshing"}) {
        reporting::GaugeHistory trace; trace.current.name=name;trace.current.percent=85.;trace.current.floor=75.;
        for(int i=0;i<=360;++i)trace.samples.append({i*10000LL,85.+10.*std::sin(i*.01)});
        trace.observed_ms=trace.active_ms=trace.target_ms=report.window_ms;
        report.nectar.append(trace);
    }
    for(const auto& buff:buffs) {
        if (!buff.level) continue;
        reporting::GaugeHistory trace;trace.current=buff;trace.current.percent=80.;
        const auto maximum=std::max(1.,buff.level->maximum);const auto phase=report.buffs.size()*.7;
        trace.observed_ms=report.window_ms;trace.peak=0.;
        for(int i=0;i<=360;++i) {
            const auto level=maximum==1?double((i+report.buffs.size()*9)%80<60):std::round(maximum*(.65+.25*std::sin(i*.025+phase)));
            trace.samples.append({i*10000LL,level/maximum*100.,{},level});
            trace.peak=std::max(*trace.peak,level);if (i && level>0) trace.active_ms+=10000;
        }
        trace.current.level->value=trace.samples.back().level;
        report.buffs.append(trace);
    }
    report.snapshot.sections={{"Planters",{{"Tacky · Pine Tree","Growing",.72},{"Pesticide · Spider","Growing",.46}},"planters"},
        {"Quests",{{"Polar Bear","Collect blue pollen",.65}},"quests"}};
    report.counts[{}]={{"Vicious Bee defeats",3,7},{"Planters harvested",4,9},{"Quests completed",2,5}};
    return report;
}
void show_report_appearance(QWidget* parent,reporting::ReportAppearance& appearance,const QString& directory,std::function<bool()> save,
    const QList<reporting::Gauge>& buffs) {
    auto dialog=new QDialog(parent);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName("report-appearance-dialog");
    dialog->setWindowTitle("Report layout");dialog->setWindowModality(Qt::WindowModal);
    auto layout=new QHBoxLayout(dialog);layout->setContentsMargins(16,16,16,16);layout->setSpacing(16);
    auto controls=new QVBoxLayout;controls->setSpacing(8);layout->addLayout(controls);
    auto fields=new QFormLayout;fields->setVerticalSpacing(8);controls->addLayout(fields);
    auto theme=new QComboBox;theme->setObjectName("report-theme");theme->addItems({"Forest (default)","Midnight","Graphite"});theme->setCurrentIndex(int(appearance.theme));fields->addRow("Theme",theme);
    auto height=new QComboBox;height->setObjectName("report-chart-height");height->addItem("Compact",75);height->addItem("Normal (default)",100);height->addItem("Tall",150);
    height->setCurrentIndex(std::max(0,height->findData(appearance.chart_height)));fields->addRow("Buff charts",height);
    theme->setMinimumHeight(28);height->setMinimumHeight(28);
    auto activity=new QCheckBox("Activity breakdown");activity->setChecked(appearance.activity);controls->addWidget(activity);
    auto nectar=new QCheckBox("Nectar");nectar->setChecked(appearance.nectar);controls->addWidget(nectar);
    auto modules=new QCheckBox("Module panels");modules->setChecked(appearance.modules);controls->addWidget(modules);
    auto totals=new QCheckBox("Totals");totals->setChecked(appearance.totals);controls->addWidget(totals);
    auto pick=new QPushButton("Choose background…");pick->setObjectName("report-background-pick");controls->addWidget(pick);
    auto remove=new QPushButton("Remove background");remove->setObjectName("report-background-remove");controls->addWidget(remove);
    auto fade_label=new QLabel;controls->addWidget(fade_label);
    auto fade=new QSlider(Qt::Horizontal);fade->setObjectName("report-background-fade");fade->setRange(0,100);fade->setValue(appearance.background_fade);controls->addWidget(fade);
    fade->setToolTip("0% keeps the image visible. 100% fades it completely into the theme.");
    auto zoom_label=new QLabel;controls->addWidget(zoom_label);
    auto zoom=new QSlider(Qt::Horizontal);zoom->setObjectName("report-background-zoom");zoom->setRange(reporting::ReportAppearance::min_background_zoom,reporting::ReportAppearance::max_background_zoom);zoom->setValue(appearance.background_zoom);controls->addWidget(zoom);
    auto reset=new QPushButton("Reset framing");reset->setObjectName("report-background-reset");controls->addWidget(reset);
    auto hint=new QLabel("Drag the background in the preview to position it. Below 100%, the theme fills any space around the image.");hint->setWordWrap(true);hint->setMaximumWidth(230);controls->addWidget(hint);
    auto error=new QLabel;error->setWordWrap(true);error->setMaximumWidth(230);error->setTextFormat(Qt::PlainText);controls->addWidget(error);controls->addStretch();
    auto done=new QPushButton("Done");controls->addWidget(done);QObject::connect(done,&QPushButton::clicked,dialog,&QDialog::accept);
    auto showcase=new QVBoxLayout;layout->addLayout(showcase,1);
    auto caption=new QLabel(QString("Your selected buffs · %1\nExample values, not your live stats").arg(buffs.size()));caption->setWordWrap(true);showcase->addWidget(caption);
    auto preview=new ReportPreview(appearance);preview->setObjectName("report-preview");
    preview->background=reporting::load_report_background(appearance.background_image);
    auto scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setWidget(preview);scroll->setMinimumWidth(490);showcase->addWidget(scroll,1);
    auto fit=new QTimer(dialog);fit->setSingleShot(true);
    QObject::connect(fit,&QTimer::timeout,dialog,[dialog,preview,scroll] {
        const auto available=dialog->screen()->availableGeometry().adjusted(16,16,-16,-16);
        const auto frame_height=dialog->frameGeometry().height()-dialog->height();
        const auto wanted=dialog->height()-scroll->viewport()->height()+preview->minimumHeight();
        dialog->resize(dialog->width(),std::min(std::max(600,wanted),available.height()-frame_height));
        const auto frame=dialog->frameGeometry();
        const auto top=std::clamp(frame.top(),available.top(),std::max(available.top(),available.bottom()-frame.height()+1));
        dialog->move(dialog->pos()+QPoint(0,top-frame.top()));
    });
    preview->size_changed=[fit] { fit->start(0); };
    auto render=new QTimer(dialog);render->setSingleShot(true);
    const auto sample=example_report(appearance,buffs);
    const auto refresh=[&appearance,preview,sample,fade_label,remove,fade,zoom,zoom_label,reset] {
        auto report=sample;report.appearance=appearance;
        preview->setReport(reporting::render_report(report,&preview->background));
        fade_label->setText(QString("Background fade · %1%").arg(appearance.background_fade));
        zoom_label->setText(QString("Background zoom · %1%").arg(appearance.background_zoom));
        remove->setEnabled(!appearance.background_image.isEmpty());fade->setEnabled(!appearance.background_image.isEmpty());
        zoom->setEnabled(!appearance.background_image.isEmpty());reset->setEnabled(!appearance.background_image.isEmpty());
    };
    QObject::connect(render,&QTimer::timeout,dialog,refresh);
    auto committed=std::make_shared<reporting::ReportAppearance>(appearance);
    const auto commit=[&appearance,committed,save,error,render] {
        if (!save()) { appearance=*committed;error->setText("Could not save report settings."); }
        else { *committed=appearance;error->clear(); }
        render->start(0);
    };
    const auto schedule=[render] { if (!render->isActive()) render->start(16); };
    preview->changed=schedule;preview->finished=commit;
    const auto changed=[&appearance,commit,theme,height,activity,nectar,modules,totals,fade,zoom,schedule] {
        appearance.theme=reporting::ReportTheme(theme->currentIndex());appearance.chart_height=height->currentData().toInt();
        appearance.activity=activity->isChecked();appearance.nectar=nectar->isChecked();appearance.modules=modules->isChecked();appearance.totals=totals->isChecked();
        appearance.background_fade=fade->value();
        appearance.background_zoom=zoom->value();
        schedule();if (!fade->isSliderDown() && !zoom->isSliderDown()) commit();
    };
    QObject::connect(theme,&QComboBox::currentIndexChanged,dialog,changed);QObject::connect(height,&QComboBox::currentIndexChanged,dialog,changed);
    for(auto check:{activity,nectar,modules,totals})QObject::connect(check,&QCheckBox::toggled,dialog,changed);
    QObject::connect(fade,&QSlider::valueChanged,dialog,changed);
    QObject::connect(zoom,&QSlider::valueChanged,dialog,changed);
    for(auto slider:{fade,zoom}) QObject::connect(slider,&QSlider::sliderReleased,dialog,commit);
    QObject::connect(reset,&QPushButton::clicked,dialog,[&appearance,zoom,commit] {
        appearance.background_x=appearance.background_y=.5;appearance.background_zoom=100;
        const QSignalBlocker block(zoom);zoom->setValue(100);commit();
    });
    QObject::connect(pick,&QPushButton::clicked,dialog,[dialog,&appearance,directory,preview,zoom,commit,error] {
        const auto file=QFileDialog::getOpenFileName(dialog,"Choose report background",{},"Images (*.png *.jpg *.jpeg *.webp)");if(file.isEmpty())return;
        QImageReader reader(file);reader.setAutoTransform(true);reader.setDecideFormatFromContent(true);
        if(!reader.size().isValid()){error->setText("That image could not be opened: "+reader.errorString());return;}
        if (reader.size().width()>2048 || reader.size().height()>2048)
            reader.setScaledSize(reader.size().scaled(2048,2048,Qt::KeepAspectRatio));
        const auto image=reader.read();if(image.isNull()){error->setText("That image could not be opened: "+reader.errorString());return;}
        const auto target=QDir(directory).filePath("report-background.png");QSaveFile output(target);
        if(!output.open(QIODevice::WriteOnly) || !image.save(&output,"PNG") || !output.commit()){error->setText("Could not save the background image.");return;}
        appearance.background_image=target;preview->background=image;
        appearance.background_x=appearance.background_y=.5;appearance.background_zoom=100;
        const QSignalBlocker block(zoom);zoom->setValue(100);commit();
    });
    QObject::connect(remove,&QPushButton::clicked,dialog,[&appearance,preview,commit] {
        appearance.background_image.clear();preview->background={};commit();
    });
    refresh();dialog->resize(810,600);dialog->show();
}
}
