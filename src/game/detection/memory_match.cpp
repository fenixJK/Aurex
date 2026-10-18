#include "memory_match.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

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
    std::vector<Rect> text_regions(const Frame&,std::uint64_t) const override { return {}; }
    std::shared_ptr<const Detection> detect(const DetectionInput& input) const override {
        const auto& frame=input.frame;
        if (!frame.valid() || frame.width>8192 || frame.height>8192 || (input.cancelled && input.cancelled())) return {};
        auto board=std::make_shared<MemoryBoard>();
        if (!continuous(frame,input.generation)) { grid_.clear(); background_.clear(); seen_=false; }
        previous_={frame.window,input.generation,frame.captured}; width_=frame.width; height_=frame.height;
        const bool tracked=background_visible(frame);
        if (!tracked) { grid_.clear(); background_.clear(); }
        if (seen_) board->visibility=Knowledge::absent;
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
                if (box.width<16 || box.width>frame.height*.16 || box.height<box.width*.85 || box.height>box.width*1.15
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
        if (!tracked) {
            // Hover/reveal animation can hide borders. The remaining cards must
            // cover every row/column and at least three quarters of a regular grid.
            if (grid.size()<12 || grid.size()>20) return board;
            std::vector<int> widths,heights;
            for (const auto& box:grid) { widths.push_back(box.width); heights.push_back(box.height); }
            std::sort(widths.begin(),widths.end()); std::sort(heights.begin(),heights.end());
            const int width=widths[widths.size()/2],height=heights[heights.size()/2];
            const auto axis=[&](bool horizontal) {
                std::vector<int> centers,lines;
                for (const auto& box:grid) centers.push_back(horizontal?box.x+box.width/2:box.y+box.height/2);
                std::sort(centers.begin(),centers.end());
                const int size=horizontal?width:height;
                for (int center:centers) if (lines.empty() || center-lines.back()>size*.3) lines.push_back(center);
                if (lines.size()<4 || lines.size()>(horizontal?5u:4u)) return std::vector<int>{};
                const double spacing=double(lines.back()-lines.front())/(lines.size()-1);
                if (spacing<size || spacing>size*1.8) return std::vector<int>{};
                for (std::size_t i=1;i<lines.size();++i)
                    if (std::abs(lines[i]-lines[i-1]-spacing)>size*.2) return std::vector<int>{};
                return lines;
            };
            const auto columns=axis(true),rows=axis(false);
            if (columns.empty() || rows.empty() || grid.size()>columns.size()*rows.size()
                || grid.size()*4<columns.size()*rows.size()*3) return board;
            std::vector<cv::Rect> complete;
            for (int y:rows) for (int x:columns) complete.emplace_back(x-width/2,y-height/2,width,height);
            grid=std::move(complete);
        }
        const auto columns=grid.size()/4;
        board->visibility=Knowledge::present;
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
