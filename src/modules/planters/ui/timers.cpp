#include "timers.hpp"
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QShowEvent>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace hive::features::planters {
namespace {
WallTime now() { return std::chrono::time_point_cast<Time>(std::chrono::system_clock::now()); }
QString pretty(const std::string& name) {
    QString result;
    for (const char c:name) {
        if (!result.isEmpty() && std::isupper(static_cast<unsigned char>(c))) result+=' ';
        result+=QChar(c);
    }
    return result;
}
QString remaining(WallTime due,WallTime current) {
    if (due<=current) return "Ready";
    const auto minutes=std::chrono::duration_cast<std::chrono::minutes>(due-current+std::chrono::seconds(59)).count();
    return QString("%1h %2m").arg(minutes/60).arg(minutes%60,2,10,QChar('0'));
}
class Timers final : public QWidget {
    std::shared_ptr<Account> account_;
    QTableWidget* table_{};
    QLabel* note_{};
    QPushButton *add_{},*edit_{},*remove_{},*retry_{};
    std::vector<Plant> plants_;
    bool editable_{},limited_{};
    void show_error(const std::exception& error) { QMessageBox::warning(this,"Planter timers",error.what()); }
    void update_controls() {
        const bool editable=editable_ && !account_->busy();
        const bool selected=table_->currentRow()>=0 && table_->currentRow()<table_->rowCount();
        add_->setEnabled(editable && plants_.size()<3);
        edit_->setEnabled(editable && selected); remove_->setEnabled(editable && selected);
        retry_->setEnabled(editable && limited_);
    }
    void refresh() {
        try {
            const auto ledger=account_->saved(); plants_=ledger.plants;
            const auto retry=account_->placement_retry();
            limited_=ledger.placement_limits.capacity.has_value() || !ledger.placement_limits.occupied_fields.empty() || (retry && now()<*retry);
            if (ledger.pending && ledger.pending->operation==Operation::place) plants_.push_back(ledger.pending->plant);
            const auto selected=table_->currentRow()>=0 && table_->currentRow()<table_->rowCount()
                ?table_->item(table_->currentRow(),0)->data(Qt::UserRole).toULongLong():0;
            table_->setRowCount(static_cast<int>(plants_.size()));
            const auto current=now();
            for (int row=0;row<static_cast<int>(plants_.size());++row) {
                const auto& plant=plants_[row];
                const bool pending=ledger.pending && ledger.pending->plant.id==plant.id;
                const auto age=std::max(0.,estimated_age(plant,current));
                const auto progress=std::min(100.,age/plant.stats.full_seconds*100.);
                const QStringList cells{pretty(plant.stats.planter),QString::fromStdString(plant.stats.field),
                    pending || plant.missing?QString("—"):QString::number(static_cast<int>(std::round(progress)))+"%",
                    plant.missing?QString("Not found"):pending?QString(ledger.pending->operation==Operation::place?"Placement unconfirmed":"Harvest unconfirmed"):remaining(ready_at(plant),current)};
                for (int col=0;col<cells.size();++col) {
                    auto* item=new QTableWidgetItem(cells[col]); item->setFlags(Qt::ItemIsEnabled|Qt::ItemIsSelectable);
                    if (col==0) item->setData(Qt::UserRole,static_cast<qulonglong>(plant.id));
                    table_->setItem(row,col,item);
                }
                if (plant.id==selected) table_->selectRow(row);
            }
            editable_=true; update_controls();
            note_->setText(account_->busy()?"Planters is active. Editing unlocks when its current run finishes.":retry && current<*retry?
                QString("Placement retry in %1. Capacity is unchanged; known harvests continue.").arg(remaining(*retry,current)):
                plants_.empty()?"No saved planters. Placed planters appear here, or use Add for one you placed yourself.":
                std::any_of(plants_.begin(),plants_.end(),[](const Plant& plant) { return plant.missing; })?"Not found: its slot stays reserved until you correct or remove the timer.":
                "Saved placements set the timers. Correct a timer after a manual change.");
            if (!ledger.placement_limits.occupied_fields.empty()) {
                QStringList fields; for (const auto& field:ledger.placement_limits.occupied_fields) fields.push_back(QString::fromStdString(field));
                note_->setText(note_->text()+" Occupied fields: "+fields.join(", ")+".");
            }
        } catch (const std::exception& error) {
            editable_=false; note_->setText(error.what()); add_->setEnabled(false); edit_->setEnabled(false); remove_->setEnabled(false); retry_->setEnabled(false);
        }
    }
    void edit(std::optional<std::uint64_t> id) {
        if (account_->busy()) return;
        const auto found=id?std::find_if(plants_.begin(),plants_.end(),[&](const Plant& value) { return value.id==*id; }):plants_.end();
        if (id && found==plants_.end()) return;
        const auto saved=id?std::optional<Plant>(*found):std::nullopt;
        QDialog dialog(this); dialog.setWindowTitle(id?"Edit planter timer":"Add planter timer"); dialog.setFixedWidth(360);
        auto layout=new QVBoxLayout(&dialog); auto form=new QFormLayout; layout->addLayout(form);
        auto type=new QComboBox; auto field=new QComboBox; auto progress=new QSpinBox;
        progress->setRange(0,100); progress->setSuffix("%");
        std::set<std::string> types;
        for (const auto& entry:catalog()) types.insert(entry.planter);
        for (const auto& name:types) type->addItem(pretty(name),QString::fromStdString(name));
        auto update_fields=[field,type] {
            const auto previous=field->currentData().toString(); field->clear();
            for (const auto& entry:catalog()) if (QString::fromStdString(entry.planter)==type->currentData().toString())
                field->addItem(QString::fromStdString(entry.field),QString::fromStdString(entry.field));
            const auto index=field->findData(previous); if (index>=0) field->setCurrentIndex(index);
        };
        connect(type,&QComboBox::currentIndexChanged,&dialog,update_fields);
        if (saved) type->setCurrentIndex(type->findData(QString::fromStdString(saved->stats.planter)));
        update_fields();
        if (saved) {
            field->setCurrentIndex(field->findData(QString::fromStdString(saved->stats.field)));
            progress->setValue(static_cast<int>(std::round(std::clamp(estimated_age(*saved,now())/saved->stats.full_seconds,0.,1.)*100)));
        }
        form->addRow("Planter",type); form->addRow("Field",field); form->addRow("Growth",progress);
        auto hint=new QLabel("Changing the planter or field starts a full-growth target."); hint->setWordWrap(true); layout->addWidget(hint);
        auto buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel); layout->addWidget(buttons);
        connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);
        connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if (dialog.exec()!=QDialog::Accepted) return;
        const auto choice=std::find_if(catalog().begin(),catalog().end(),[&](const Stats& value) {
            return QString::fromStdString(value.planter)==type->currentData().toString()
                && QString::fromStdString(value.field)==field->currentData().toString();
        });
        if (choice==catalog().end()) return;
        try { account_->set_saved(id,*choice,progress->value()/100.,now()); refresh(); }
        catch (const std::exception& error) { show_error(error); }
    }
protected:
    void showEvent(QShowEvent* event) override { QWidget::showEvent(event); refresh(); }
public:
    Timers(std::shared_ptr<Account> account,QWidget* parent):QWidget(parent),account_(std::move(account)) {
        setWindowTitle("Timers");
        auto layout=new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
        table_=new QTableWidget(0,4); table_->setHorizontalHeaderLabels({"Planter","Field","Growth","Target"});
        table_->setSelectionBehavior(QAbstractItemView::SelectRows); table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->verticalHeader()->hide(); table_->horizontalHeader()->setStretchLastSection(true);
        table_->setColumnWidth(0,150); table_->setColumnWidth(1,140); table_->setColumnWidth(2,75); layout->addWidget(table_);
        note_=new QLabel; note_->setWordWrap(true); layout->addWidget(note_);
        auto controls=new QHBoxLayout; layout->addLayout(controls);
        add_=new QPushButton("Add"); edit_=new QPushButton("Edit"); remove_=new QPushButton("Collected");
        retry_=new QPushButton("Retry placements");
        controls->addWidget(add_); controls->addWidget(edit_); controls->addWidget(remove_); controls->addWidget(retry_); controls->addStretch();
        connect(retry_,&QPushButton::clicked,this,[this] {
            try { account_->retry_placements(); refresh(); } catch (const std::exception& error) { show_error(error); }
        });
        connect(add_,&QPushButton::clicked,this,[this] { edit({}); });
        connect(edit_,&QPushButton::clicked,this,[this] {
            const auto row=table_->currentRow(); if (row>=0 && row<static_cast<int>(plants_.size())) edit(plants_[row].id);
        });
        connect(remove_,&QPushButton::clicked,this,[this] {
            const auto row=table_->currentRow(); if (row<0 || row>=static_cast<int>(plants_.size())) return;
            const auto plant=plants_[row];
            if (QMessageBox::question(this,"Mark collected","Remove the saved timer for "+pretty(plant.stats.planter)+" in "+QString::fromStdString(plant.stats.field)+"?")!=QMessageBox::Yes) return;
            try { account_->remove_saved(plant.id); refresh(); } catch (const std::exception& error) { show_error(error); }
        });
        connect(table_,&QTableWidget::itemSelectionChanged,this,[this] { update_controls(); });
        auto timer=new QTimer(this); connect(timer,&QTimer::timeout,this,[this] { if (isVisible()) refresh(); }); timer->start(1000);
        refresh();
    }
};
}
QWidget* planter_timers(std::shared_ptr<Account> account,QWidget* parent) {
    return new Timers(std::move(account),parent);
}
}
