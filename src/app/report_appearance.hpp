#pragma once
#include "reporting/report.hpp"
#include <functional>
class QWidget;
namespace hive::app {
reporting::Report example_report(const reporting::ReportAppearance& appearance);
void show_report_appearance(QWidget* parent,reporting::ReportAppearance& appearance,const QString& data_directory,std::function<bool()> save);
}
