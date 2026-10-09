#pragma once
#include "core/module_host/module.hpp"
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <algorithm>

namespace hive {
class ModuleMetrics final : public QWidget {
    struct Row { QLabel* value; QProgressBar* progress; };
    std::vector<Row> rows_;
    std::vector<extensions::Metric> previous_;
    QVBoxLayout* layout_;
    static QLabel* label(const std::string& value,const char* role=nullptr) {
        auto text=new QLabel(QString::fromStdString(value)); text->setTextFormat(Qt::PlainText); text->setWordWrap(true);
        if (role) text->setObjectName(role); return text;
    }
public:
    explicit ModuleMetrics(QWidget* parent=nullptr):QWidget(parent),layout_(new QVBoxLayout(this)) {
        layout_->setContentsMargins(0,0,0,0); layout_->setSpacing(8);
        layout_->addWidget(label("No activity yet","muted"));
    }
    void setMetrics(const std::vector<extensions::Metric>& values) {
        const bool same=previous_.size()==values.size() && std::equal(values.begin(),values.end(),previous_.begin(),[](const auto& a,const auto& b) {
            return a.label==b.label && a.group==b.group && a.progress.has_value()==b.progress.has_value();
        });
        if (!same) {
            while (auto* item=layout_->takeAt(0)) { delete item->widget(); delete item; }
            rows_.clear();
            if (values.empty()) layout_->addWidget(label("No activity yet","muted"));
            std::map<std::string,QVBoxLayout*> groups;
            for (const auto& metric:values) {
                if (!groups.contains(metric.group)) {
                    auto card=new QWidget; card->setObjectName("card");
                    auto body=new QVBoxLayout(card); body->setContentsMargins(12,10,12,10); body->setSpacing(7);
                    if (!metric.group.empty()) body->addWidget(label(metric.group,"section-heading"));
                    groups[metric.group]=body; layout_->addWidget(card);
                }
                auto body=groups.at(metric.group);
                auto line=new QHBoxLayout; line->setSpacing(12);
                line->addWidget(label(metric.label,"muted"),1);
                auto value=label(metric.value); value->setAlignment(Qt::AlignRight|Qt::AlignVCenter); line->addWidget(value,1);
                body->addLayout(line);
                QProgressBar* progress{};
                if (metric.progress) {
                    progress=new QProgressBar; progress->setObjectName("module-progress"); progress->setRange(0,1000);
                    progress->setTextVisible(false); progress->setFixedHeight(5); body->addWidget(progress);
                }
                rows_.push_back({value,progress});
            }
        }
        for (std::size_t i=0;i<values.size();++i) {
            rows_[i].value->setText(QString::fromStdString(values[i].value));
            if (rows_[i].progress) rows_[i].progress->setValue(int(*values[i].progress*1000));
        }
        previous_=values;
    }
};
}
