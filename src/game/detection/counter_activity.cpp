#include "counter_activity.hpp"
#include <opencv2/imgproc.hpp>
#include <algorithm>

namespace hive::game {
std::optional<Time> CounterActivity::observe(const Frame& frame,Rect pixels) {
    if (!frame.valid() || !pixels.valid() || frame.captured<captured_) return {};
    // Exclude the outline and normalize UI scaling before comparing glyphs.
    const cv::Rect region(int(pixels.x+pixels.width*.02),int(pixels.y+pixels.height*.2),
        int(pixels.width*.96),int(pixels.height*.6));
    if (region.width<1 || region.height<1 || region.x<0 || region.y<0
        || region.br().x>frame.width || region.br().y>frame.height) return {};
    const cv::Mat image(frame.height,frame.width,CV_8UC4,const_cast<std::uint8_t*>(frame.rgba.data()));
    cv::Mat normalized,mask;
    cv::resize(image(region),normalized,{220,18},0,0,cv::INTER_AREA);
    cv::inRange(normalized,cv::Scalar(210,210,210,240),cv::Scalar(255,255,255,255),mask);
    const int ink=cv::countNonZero(mask);
    if (ink<20 || ink>mask.total()/2) return {};
    bool changed=previous_.empty();
    if (!changed) {
        const cv::Mat previous(mask.rows,mask.cols,CV_8UC1,previous_.data());
        cv::Mat before,after,added,removed;
        // One pixel of tolerance ignores antialiasing against the moving world
        // behind the translucent HUD without accepting different digits.
        const auto tolerance=cv::getStructuringElement(cv::MORPH_CROSS,{3,3});
        cv::dilate(previous,before,tolerance); cv::dilate(mask,after,tolerance);
        cv::bitwise_and(mask,~before,added); cv::bitwise_and(previous,~after,removed);
        changed=cv::countNonZero(added)+cv::countNonZero(removed)>std::max(3.,ink*.02);
    }
    if (changed) { previous_.assign(mask.datastart,mask.dataend); changed_=frame.captured; }
    captured_=frame.captured;
    return changed_;
}
}
