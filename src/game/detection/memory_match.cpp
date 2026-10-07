#include "hive/game/detection/memory_match.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cctype>

namespace hive::game {
double card_distance(const CardImage& a,const CardImage& b) {
    double total=0;
    for (std::size_t i=0;i<a.size();++i) total+=std::abs(int(a[i])-int(b[i]));
    return total/a.size();
}
namespace {
class BoardDetector final : public Detector {
    mutable ObservationContext previous_;
    mutable int width_{},height_{};
    mutable std::vector<cv::Rect> grid_;
    mutable std::vector<std::pair<cv::Point,cv::Vec3b>> background_;
    mutable bool seen_{};
    bool continuous(const Frame& frame,std::uint64_t generation) const {
        return width_==frame.width && height_==frame.height && previous_.generation==generation && previous_.window.id==frame.window.id
            && previous_.window.process==frame.window.process && previous_.window.bounds==frame.window.bounds
            && frame.captured>=previous_.now && frame.captured-previous_.now<=Time(5000);
    }
    cv::Vec3b pixel(const Frame& frame,cv::Point point) const {
        const auto at=(std::size_t(point.y)*frame.width+point.x)*4;
        return {frame.rgba[at],frame.rgba[at+1],frame.rgba[at+2]};
    }
    bool background_visible(const Frame& frame) const {
        unsigned matching=0;
        for (const auto& [point,color]:background_) {
            const auto now=pixel(frame,point);
            int difference=0; for (unsigned channel=0;channel<3;++channel) difference+=std::abs(int(now[channel])-int(color[channel]));
            matching+=difference<=45;
        }
        return !background_.empty() && matching*4>=background_.size()*3;
    }
public:
    std::vector<Rect> text_regions(const Frame& frame,std::uint64_t generation) const override {
        if (frame.valid() && continuous(frame,generation) && background_visible(frame)) return {};
        return {{.2,.18,.6,.22}};
    }
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        const auto& frame=input.frame;
        if (!frame.valid() || frame.width>8192 || frame.height>8192 || input.text.size()>4096 || (input.cancelled && input.cancelled())) return {};
        auto board=std::make_shared<MemoryBoard>();
        if (!continuous(frame,input.generation)) { grid_.clear(); background_.clear(); seen_=false; }
        previous_={frame.window,input.generation,frame.captured}; width_=frame.width; height_=frame.height;
        bool title=false;
        for (const auto& line:input.text) {
            std::string text=line.text;
            std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c) { return std::tolower(c); });
            if (text.find("memory match")!=std::string::npos && line.pixels.y>=frame.height*.18 && line.pixels.y<frame.height*.4) title=true;
        }
        const bool tracked=background_visible(frame);
        if (!tracked) { grid_.clear(); background_.clear(); }
        if (!title && !tracked) { if (seen_) board->visibility=Knowledge::absent; return board; }
        board->visibility=Knowledge::present;
        cv::Mat rgba(frame.height,frame.width,CV_8UC4,const_cast<std::uint8_t*>(frame.rgba.data())),gray,edges;
        std::vector<cv::Rect> grid=grid_;
        if (grid.empty()) {
            cv::cvtColor(rgba,gray,cv::COLOR_RGBA2GRAY);
            cv::Canny(gray,edges,40,100);
            std::vector<std::vector<cv::Point>> contours;
            cv::findContours(edges,contours,cv::RETR_LIST,cv::CHAIN_APPROX_SIMPLE);
            std::vector<cv::Rect> boxes;
            for (const auto& contour:contours) {
                const auto box=cv::boundingRect(contour);
                if (box.width<frame.height*.035 || box.width>frame.height*.16 || box.height<box.width*.85 || box.height>box.width*1.15
                    || box.x<frame.width*.15 || box.x+box.width>frame.width*.9 || box.y<frame.height*.2 || box.y+box.height>frame.height*.9) continue;
                if (cv::contourArea(contour)<box.area()*.85) continue;
                if (std::any_of(boxes.begin(),boxes.end(),[&](const auto& old) { return (box&old).area()>std::min(box.area(),old.area())*.8; })) continue;
                boxes.push_back(box);
            }
            if (boxes.size()>100) return board;
            for (const auto& seed:boxes) {
                std::vector<cv::Rect> same;
                for (const auto& box:boxes)
                    if (std::abs(box.width-seed.width)<seed.width*.12 && std::abs(box.height-seed.height)<seed.height*.12) same.push_back(box);
                if (same.size()>grid.size()) grid=std::move(same);
            }
        }
        if (grid.size()!=16 && grid.size()!=20) return board;
        const auto columns=grid.size()/4;
        std::vector<cv::Rect> previous_row;
        unsigned rows=0;
        std::sort(grid.begin(),grid.end(),[](const auto& a,const auto& b) { return a.y<b.y; });
        for (auto first=grid.begin();first!=grid.end();) {
            auto last=std::find_if(first,grid.end(),[&](const auto& box) { return box.y-first->y>first->height*.3; });
            if (std::size_t(last-first)!=columns) return board;
            std::sort(first,last,[](const auto& a,const auto& b) { return a.x<b.x; });
            const int spacing=(first+1)->x-first->x;
            if (spacing<first->width || spacing>first->width*1.8) return board;
            for (auto item=first+1;item!=last;++item)
                if (std::abs(item->x-(item-1)->x-spacing)>first->width*.2) return board;
            if (!previous_row.empty()) for (unsigned i=0;i<columns;++i)
                if (std::abs((first+i)->x-previous_row[i].x)>first->width*.2
                    || std::abs((first+i)->y-previous_row[i].y-spacing)>first->height*.25) return board;
            previous_row.assign(first,last); ++rows; first=last;
        }
        if (rows!=4) return board;
        grid_=grid;
        seen_=true;
        if (background_.empty()) for (unsigned i=0;i<grid.size();++i) {
            if (i%columns==columns-1) continue;
            const auto& left=grid[i]; const auto& right=grid[i+1];
            const cv::Point point{(left.x+left.width+right.x)/2,left.y+left.height/2};
            background_.push_back({point,pixel(frame,point)});
        }
        for (const auto& box:grid) {
            if (input.cancelled && input.cancelled()) return {};
            const int inset=std::max(2,box.width/8);
            cv::Mat rgb,small;
            cv::cvtColor(rgba(cv::Rect(box.x+inset,box.y+inset,box.width-2*inset,box.height-2*inset)),rgb,cv::COLOR_RGBA2RGB);
            cv::resize(rgb,small,{16,16},0,0,cv::INTER_AREA);
            MemoryCard card{{double(box.x)/frame.width,double(box.y)/frame.height,double(box.width)/frame.width,double(box.height)/frame.height},{}};
            std::copy_n(small.ptr<std::uint8_t>(),card.image.size(),card.image.begin()); board->cards.push_back(card);
        }
        return board;
    }
};
}
std::shared_ptr<const Detector> memory_match_detector() { return std::make_shared<BoardDetector>(); }
}
