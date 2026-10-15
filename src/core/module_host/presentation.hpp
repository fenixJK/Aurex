#pragma once
#include "commands.hpp"
namespace hive::extensions {
struct PlotSeries { std::string label; std::vector<Point> points; std::string color; };
struct PlotTable {
    std::string title;
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
    std::string note;
    bool with_chart{};
};
struct Plot {
    std::string title,description,x_label,y_label;
    double x_max{1},y_max{100};
    std::vector<PlotSeries> series;
    std::vector<std::pair<std::string,std::string>> statistics;
    std::vector<PlotTable> tables;
};
}
