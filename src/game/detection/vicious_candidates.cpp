#include "vicious.hpp"
#include <opencv2/imgproc.hpp>
#include <array>

namespace hive::game {
std::vector<ViciousCandidate> vicious_candidates(const Frame& frame) {
    if (!frame.valid() || frame.region!=Rect{0,0,1,1}) return {};
    // Body, face and stinger colours from the user's scripts.7z colour audit.
    // Match small connected patches, not the archive's single-pixel positives.
    static constexpr std::uint32_t colors[]{
        0x1c3240,0x1c3140,0x152834,0x1d3442,0x1b303f,0x1b3140,0x1d3342,0x1e3544,
        0x043337,0x043438,0x053c3f,0x063f42,0x022228,0x04373a,0x839d9e,0xa9a94a,
        0x889d83,0x9a9a44,0xbce2e8,0x9e9e46,0x9fc4c4,0x47575b,0x283336,0xbde7f1,
        0xbce6ef,0x4b5d67,0xacd4dd,0xc7c99c,0x44545d,0x5e7788,0x536977,0x556b7a,
        0x556c7b,0x576c7a,0x576d7b,0x5d7586,0xb4dde6,0xb5dee7,0xd2d49f,0xd3d69f,
        0xd4d69f,0xd5d7a0,0x5d7687,0xdcdea1,0x92b5bd,0x8eb1b8,0x8eb0b7,0xb2b24d,
        0xb3b34e,0xc7c755,0xcaca56,0xbec26a,0xc4c455,0xc7cb6e,0xcaca57,0xb6b968,
        0xb9bd69,0xb9be69,0xbaba50};
    static const auto lookup=[] {
        std::array<std::uint8_t,64*64*64> result{};
        for (const auto rgb:colors) {
            const int r=rgb>>16,g=(rgb>>8)&255,b=rgb&255;
            for (int dr=-3;dr<=3;++dr) for (int dg=-3;dg<=3;++dg) for (int db=-3;db<=3;++db)
                result[(std::clamp(r+dr,0,255)>>2)*4096+(std::clamp(g+dg,0,255)>>2)*64+(std::clamp(b+db,0,255)>>2)]=1;
        }
        return result;
    }();
    const cv::Mat original(frame.height,frame.width,CV_8UC4,const_cast<std::uint8_t*>(frame.rgba.data()));
    cv::Mat pixels;
    const double scale=std::min(1.,1280./frame.width);
    cv::resize(original,pixels,{},scale,scale,cv::INTER_NEAREST);
    cv::Mat mask(pixels.rows,pixels.cols,CV_8UC1,cv::Scalar(0));
    for (int y=int(pixels.rows*.18);y<int(pixels.rows*.80);++y)
        for (int x=int(pixels.cols*.24);x<int(pixels.cols*.95);++x) {
            const auto p=pixels.at<cv::Vec4b>(y,x);
            if (p[3]>=240 && lookup[(p[0]>>2)*4096+(p[1]>>2)*64+(p[2]>>2)]) mask.at<std::uint8_t>(y,x)=255;
        }
    cv::Mat grouped;
    cv::dilate(mask,grouped,cv::getStructuringElement(cv::MORPH_RECT,{3,3}));
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(grouped,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    std::vector<ViciousCandidate> found;
    for (const auto& contour:contours) {
        const auto box=cv::boundingRect(contour);
        if (box.width<4 || box.height<4 || box.width>pixels.cols*.18 || box.height>pixels.rows*.28) continue;
        unsigned count=0;double x=0,y=0;int first_tone=-1;bool varied=false;
        for (int row=box.y;row<box.y+box.height;++row) for (int col=box.x;col<box.x+box.width;++col)
            if (mask.at<std::uint8_t>(row,col)) {
                ++count;x+=col;y+=row;
                const auto p=pixels.at<cv::Vec4b>(row,col);
                const int tone=(p[0]>>2)*4096+(p[1]>>2)*64+(p[2]>>2);
                if (first_tone<0) first_tone=tone;
                else varied|=tone!=first_tone;
            }
        if (count<5 || !varied) continue;
        found.push_back({{x/count/pixels.cols,y/count/pixels.rows},double(std::max(box.width,box.height))/pixels.cols,count});
    }
    std::sort(found.begin(),found.end(),[](const auto& a,const auto& b) { return a.pixels>b.pixels; });
    if (found.size()>32) found.resize(32);
    return found;
}
}
