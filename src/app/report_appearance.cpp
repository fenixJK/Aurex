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
#include <cmath>

namespace hive::app {
reporting::Report example_report(const reporting::ReportAppearance& appearance) {
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
    for(const auto& name:{"Blue boost","Haste"}) {
        reporting::GaugeHistory trace;trace.current.name=name;trace.current.id=name;trace.current.percent=80.;trace.current.level=reporting::Level{8,10};
        trace.observed_ms=trace.active_ms=report.window_ms;
        for(int i=0;i<=360;++i)trace.samples.append({i*10000LL,65.+25.*std::sin(i*.025),{},6.5+2.5*std::sin(i*.025)});
        report.buffs.append(trace);
    }
    report.snapshot.sections={{"Planters",{{"Tacky · Pine Tree","Growing",.72},{"Pesticide · Spider","Growing",.46}},"planters"},
        {"Quests",{{"Polar Bear","Collect blue pollen",.65}},"quests"}};
    report.counts[{}]={{"Vicious Bee defeats",3,7},{"Planters harvested",4,9},{"Quests completed",2,5}};
    return report;
}
void show_report_appearance(QWidget* parent,reporting::ReportAppearance& appearance,const QString& directory,std::function<bool()> save) {
    auto dialog=new QDialog(parent);dialog->setAttribute(Qt::WA_DeleteOnClose);dialog->setObjectName("report-appearance-dialog");
    dialog->setWindowTitle("Report layout");dialog->setWindowModality(Qt::WindowModal);
    auto layout=new QHBoxLayout(dialog);layout->setContentsMargins(16,16,16,16);layout->setSpacing(16);
    auto controls=new QVBoxLayout;layout->addLayout(controls);
    auto fields=new QFormLayout;controls->addLayout(fields);
    auto theme=new QComboBox;theme->setObjectName("report-theme");theme->addItems({"Forest (default)","Midnight","Graphite"});theme->setCurrentIndex(int(appearance.theme));fields->addRow("Theme",theme);
    auto height=new QComboBox;height->setObjectName("report-chart-height");height->addItem("Compact",75);height->addItem("Normal (default)",100);height->addItem("Tall",150);
    height->setCurrentIndex(std::max(0,height->findData(appearance.chart_height)));fields->addRow("Buff charts",height);
    auto activity=new QCheckBox("Activity breakdown");activity->setChecked(appearance.activity);controls->addWidget(activity);
    auto nectar=new QCheckBox("Nectar");nectar->setChecked(appearance.nectar);controls->addWidget(nectar);
    auto modules=new QCheckBox("Module panels");modules->setChecked(appearance.modules);controls->addWidget(modules);
    auto totals=new QCheckBox("Totals");totals->setChecked(appearance.totals);controls->addWidget(totals);
    auto pick=new QPushButton("Choose background…");pick->setObjectName("report-background-pick");controls->addWidget(pick);
    auto remove=new QPushButton("Remove background");remove->setObjectName("report-background-remove");controls->addWidget(remove);
    auto fade_label=new QLabel;controls->addWidget(fade_label);
    auto fade=new QSlider(Qt::Horizontal);fade->setObjectName("report-background-fade");fade->setRange(0,100);fade->setValue(appearance.background_fade);controls->addWidget(fade);
    fade->setToolTip("0% keeps the image visible. 100% fades it completely into the theme.");
    auto error=new QLabel;error->setWordWrap(true);error->setMaximumWidth(230);error->setTextFormat(Qt::PlainText);controls->addWidget(error);controls->addStretch();
    auto done=new QPushButton("Done");controls->addWidget(done);QObject::connect(done,&QPushButton::clicked,dialog,&QDialog::accept);
    auto showcase=new QVBoxLayout;layout->addLayout(showcase,1);
    auto caption=new QLabel("Example report · sample data, not your live stats");caption->setWordWrap(true);showcase->addWidget(caption);
    auto preview=new QLabel;preview->setObjectName("report-preview");preview->setAlignment(Qt::AlignTop|Qt::AlignHCenter);
    auto scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setWidget(preview);scroll->setMinimumWidth(490);showcase->addWidget(scroll,1);
    auto render=new QTimer(dialog);render->setSingleShot(true);
    const auto refresh=[&appearance,preview,fade_label,remove,fade] {
        auto image=reporting::render_report(example_report(appearance));
        preview->setPixmap(QPixmap::fromImage(image.scaledToWidth(480,Qt::SmoothTransformation)));
        fade_label->setText(QString("Background fade · %1%").arg(appearance.background_fade));
        remove->setEnabled(!appearance.background_image.isEmpty());fade->setEnabled(!appearance.background_image.isEmpty());
    };
    QObject::connect(render,&QTimer::timeout,dialog,refresh);
    const auto changed=[&appearance,save,theme,height,activity,nectar,modules,totals,fade,render,error] {
        const auto previous=appearance;
        appearance.theme=reporting::ReportTheme(theme->currentIndex());appearance.chart_height=height->currentData().toInt();
        appearance.activity=activity->isChecked();appearance.nectar=nectar->isChecked();appearance.modules=modules->isChecked();appearance.totals=totals->isChecked();
        appearance.background_fade=fade->value();
        if(!save()){appearance=previous;error->setText("Could not save report settings.");}else error->clear();
        render->start(60);
    };
    QObject::connect(theme,&QComboBox::currentIndexChanged,dialog,changed);QObject::connect(height,&QComboBox::currentIndexChanged,dialog,changed);
    for(auto check:{activity,nectar,modules,totals})QObject::connect(check,&QCheckBox::toggled,dialog,changed);
    QObject::connect(fade,&QSlider::valueChanged,dialog,changed);
    QObject::connect(pick,&QPushButton::clicked,dialog,[dialog,&appearance,directory,save,render,error] {
        const auto file=QFileDialog::getOpenFileName(dialog,"Choose report background",{},"Images (*.png *.jpg *.jpeg)");if(file.isEmpty())return;
        QImageReader reader(file);reader.setAutoTransform(true);
        if(!reader.size().isValid()){error->setText("That image could not be opened.");return;}
        reader.setScaledSize(reader.size().scaled(2048,2048,Qt::KeepAspectRatio));
        const auto image=reader.read();if(image.isNull()){error->setText("That image could not be opened.");return;}
        const auto target=QDir(directory).filePath("report-background.png");QSaveFile output(target);
        if(!output.open(QIODevice::WriteOnly) || !image.save(&output,"PNG") || !output.commit()){error->setText("Could not save the background image.");return;}
        const auto previous=appearance.background_image;appearance.background_image=target;
        if(!save()){appearance.background_image=previous;error->setText("Could not save report settings.");}else error->clear();
        render->start(0);
    });
    QObject::connect(remove,&QPushButton::clicked,dialog,[&appearance,save,render,error] {
        const auto previous=appearance.background_image;appearance.background_image.clear();
        if(!save()){appearance.background_image=previous;error->setText("Could not save report settings.");}else error->clear();
        render->start(0);
    });
    refresh();dialog->resize(810,600);dialog->show();
}
}
