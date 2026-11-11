#include "buff_stack.hpp"
#include "buff_counters.hpp"
#include "buff_counter_variants.hpp"
#include "native_buff_counters.hpp"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <span>

namespace hive::game::detail {
namespace {
constexpr double minimum_quality=.35,minimum_margin=.03;
constexpr int caption_slack=3,expanded_caption_slack=6;
struct CaptionFont {std::span<const BuffCounter> counters;double calibration;};
constexpr std::array caption_fonts{
    CaptionFont{buff_counters,0},CaptionFont{buff_bold_counters,.175},
    CaptionFont{buff_native_counters_18,.175},CaptionFont{buff_native_counters_17,.175}
};
struct CaptionRanking {
    unsigned value{};
    double best{-1000},second{-1000};
    bool distinct()const{return value && best-second>minimum_margin;}
    // Style calibration ranks competing appearances. It must not also lower
    // the measured match quality used to admit a candidate.
    bool confident(double calibration=0)const{return distinct() && best+calibration>minimum_quality;}
};
CaptionRanking rank_captions(std::span<const double> qualities,unsigned first=1) {
    CaptionRanking result;
    for(unsigned i=first;i<qualities.size();++i) {
        if(qualities[i]>result.best){result.second=result.best;result.best=qualities[i];result.value=i;}
        else result.second=std::max(result.second,qualities[i]);
    }
    return result;
}
struct CaptionReference {
    unsigned value,style;
    cv::Mat image,outline,strokes,distance;
    unsigned stroke_pixels;
    unsigned number_offset;
    double calibration;
};
const std::vector<CaptionReference>& caption_references() {
    static const auto references=[] {
        std::vector<CaptionReference> result;
        const auto add=[&](const auto& counters,double calibration,unsigned style) {
            for(const auto& counter:counters) {
                if(!counter.number_offset || counter.number_offset>=unsigned(counter.width))continue;
                CaptionReference r;r.value=counter.value;r.style=style;r.calibration=calibration;r.number_offset=counter.number_offset;
                r.image.create(counter.height,counter.width,CV_32F);
                for(int y=0;y<r.image.rows;++y)for(int x=0;x<r.image.cols;++x)
                    r.image.at<float>(y,x)=((counter.rows[y]>>(x*2))&3)/3.f;
                cv::Mat outline,background;
                cv::dilate(r.image,outline,cv::Mat::ones(3,3,CV_8U));
                cv::compare(outline,.1,r.outline,cv::CMP_GT);
                cv::compare(r.image,.3,r.strokes,cv::CMP_GT);
                cv::bitwise_not(r.strokes,background);
                cv::distanceTransform(background,r.distance,cv::DIST_L2,5);
                r.stroke_pixels=cv::countNonZero(r.strokes);
                result.push_back(std::move(r));
            }
        };
        // Wider, heavier references fit several neighboring digit shapes more
        // readily. Calibrate that reference bank's score, without a retry or
        // a different acceptance rule for its font or the buff using it.
        for(unsigned i=0;i<caption_fonts.size();++i)
            add(caption_fonts[i].counters,caption_fonts[i].calibration,i);
        return result;
    }();
    return references;
}
struct ObservedStrokes {cv::Mat pixels,distance;unsigned count{};};
struct CaptionSample {
    cv::Mat gray,ink,strokes;
    int right_edge{},absence_width{};
    std::map<std::array<int,4>,ObservedStrokes> observations;
};
struct CaptionMatch {double quality{-1000},outline{};};

double aligned_peak(const cv::Mat& scores,int caption_width,int right_edge,int right_slack=expanded_caption_slack) {
    double peak=0;
    for(int y=0;y<scores.rows;++y)for(int x=0;x<scores.cols;++x)
        if(x+caption_width-right_edge>=-caption_slack && x+caption_width-right_edge<=right_slack)
            if(const auto score=scores.at<float>(y,x);std::isfinite(score))peak=std::max(peak,double(score));
    return peak;
}

std::optional<CaptionSample> sample_caption(const Frame& frame,Rect cell,double scale) {
    const int left=std::max(0,int(std::floor(cell.x-caption_slack*scale))),top=std::max(0,int(std::floor(cell.y+18*scale)));
    const int right=std::min(frame.width,int(std::ceil(cell.x+(38+expanded_caption_slack)*scale))),bottom=std::min(frame.height,int(std::ceil(cell.y+42*scale)));
    if(right<=left || bottom<=top)return {};
    const cv::Mat rgba(frame.height,frame.width,CV_8UC4,const_cast<std::uint8_t*>(frame.rgba.data()));
    cv::Mat normalized,gray,strokes;
    cv::resize(rgba(cv::Rect(left,top,right-left,bottom-top)),normalized,cv::Size(),1/scale,1/scale,cv::INTER_AREA);
    cv::cvtColor(normalized,gray,cv::COLOR_RGBA2GRAY);gray.convertTo(gray,CV_32F,1./255);
    cv::Mat ink(normalized.rows,normalized.cols,CV_32F);
    // Minimum-channel brightness measures white ink continuously. A hard
    // chroma cutoff drops antialiased letter edges blended with colored tiles.
    for(int y=0;y<normalized.rows;++y)for(int x=0;x<normalized.cols;++x) {
        const auto p=normalized.at<cv::Vec4b>(y,x);
        const int lo=std::min({p[0],p[1],p[2]});
        ink.at<float>(y,x)=p[3]>=240?std::clamp((lo-110)/133.f,0.f,1.f):0;
    }
    cv::compare(ink,.3,strokes,cv::CMP_GT);
    // Wider captions may extend into their neighbor's tile. That extra area
    // can confirm a number, but must not decide whether this tile has no number.
    const int absence_right=std::min(frame.width,int(std::ceil(cell.x+(38+caption_slack)*scale)));
    return CaptionSample{std::move(gray),std::move(ink),std::move(strokes),int(std::round((cell.x+cell.width-left)/scale)),
        int(std::round((absence_right-left)/scale)),{}};
}

CaptionMatch match_caption(const CaptionReference& reference,CaptionSample& sample) {
    auto& [full_gray,full_ink,strokes,full_edge,absence_width,observations]=sample;
    CaptionMatch result;
    double strongest_outline=0;
    const int width=reference.image.cols,height=reference.image.rows;
    if(width>full_gray.cols || height>full_gray.rows)return result;
    // Only right-aligned positions can be valid. Restrict the search before
    // correlation instead of computing matches that would later be discarded.
    const int left=std::max(0,full_edge-width-caption_slack);
    const int last=std::min(full_gray.cols-width,full_edge-width+expanded_caption_slack);
    if(last<left)return result;
    const auto gray=full_gray.colRange(left,last+width);
    const auto ink=full_ink.colRange(left,last+width);
    const int edge=full_edge-left;
    cv::Mat outlines,inks,digits,markers;
    cv::matchTemplate(gray,reference.image,outlines,cv::TM_CCOEFF_NORMED,reference.outline);
    // Reject impossible captions before the more expensive stroke comparisons.
    // Preserve outline evidence even on rejection: unreadable text is not x1.
    result.outline=aligned_peak(outlines,width,edge,caption_slack);
    if(aligned_peak(outlines,width,edge)<.45)return result;
    const int marker_width=reference.number_offset;
    const cv::Rect number(marker_width,0,width-marker_width,height);
    const cv::Rect marker(0,0,marker_width,height);
    cv::matchTemplate(gray,reference.image(marker),markers,cv::TM_CCOEFF_NORMED,reference.outline(marker));
    if(aligned_peak(markers(cv::Rect(0,0,outlines.cols,outlines.rows)),width,edge)<=.45)return result;
    cv::Mat nearby_markers;cv::dilate(markers,nearby_markers,cv::Mat::ones(3,3,CV_8U));
    // The shared multiplier marker locates the caption; compare white ink
    // within the number so matching the x cannot inflate a wrong count.
    cv::matchTemplate(ink,reference.image(number),inks,cv::TM_CCOEFF_NORMED,reference.outline(number));
    cv::matchTemplate(gray,reference.image(number),digits,cv::TM_CCOEFF_NORMED,reference.outline(number));
    double contrast=0,ink_fit=0,digit_fit=0,marker_fit=0,shape_error=1000;
    for(int y=0;y<outlines.rows;++y)for(int x=0;x<outlines.cols;++x) {
        // The right edge rejects prefixes of larger counts and neighbors.
        if(x+width-edge < -caption_slack || x+width-edge > expanded_caption_slack)continue;
        const double outline=outlines.at<float>(y,x),ink_score=inks.at<float>(y,x+marker_width),digit=digits.at<float>(y,x+marker_width);
        if(!std::isfinite(outline))continue;
        if(x+width-edge<=caption_slack)strongest_outline=std::max(strongest_outline,outline);
        contrast=std::max(contrast,outline);
        const double marker_score=markers.at<float>(y,x);
        if(std::isfinite(marker_score))marker_fit=std::max(marker_fit,marker_score);
        if(std::isfinite(ink_score))ink_fit=std::max(ink_fit,ink_score);
        if(std::isfinite(digit) && nearby_markers.at<float>(y,x)>.45)digit_fit=std::max(digit_fit,digit);
        if(outline<.45)continue;
        auto [it,inserted]=observations.try_emplace(std::array{x+left,y,width,height});
        auto& observed=it->second;
        if(inserted) {
            observed.pixels=strokes(cv::Rect(x+left,y,width,height));
            observed.count=cv::countNonZero(observed.pixels);
            if(observed.count) {
                cv::Mat background;cv::bitwise_not(observed.pixels,background);
                cv::distanceTransform(background,observed.distance,cv::DIST_L2,5);
            }
        }
        if(!observed.count)continue;
        const double missing=cv::mean(observed.distance,reference.strokes)[0];
        const double extra=cv::mean(reference.distance,observed.pixels)[0];
        // Scaled/compressed lettering provides less stroke evidence.
        // Downweight geometry when only a few white pixels survive.
        const double coverage=std::min(1.,2.*observed.count/reference.stroke_pixels);
        shape_error=std::min(shape_error,(.8*missing+.2*extra)*coverage*coverage);
    }
    // Require the multiplier marker at this caption's width. Without it,
    // the final digit of x47 could outscore the complete two-digit caption.
    if(marker_fit<=.45){result.outline=strongest_outline;return result;}
    const double quality=.1*contrast+.3*ink_fit+.6*digit_fit-.1*shape_error-reference.calibration;
    result.quality=quality;result.outline=strongest_outline;
    return result;
}

double uncaptioned_quality(const CaptionSample& sample,const CaptionReference& reference,double strongest_outline) {
    const int right=std::min(sample.gray.cols,sample.absence_width);
    const auto ink=sample.ink.colRange(0,right),gray=sample.gray.colRange(0,right);
    // A buff with no printed multiplier is a one-stack candidate. It must
    // compete against the captions, rather than defaulting to one on failure.
    if(ink.cols>=int(reference.number_offset) && ink.rows>=reference.image.rows) {
        const cv::Rect region(0,0,reference.number_offset,reference.image.rows);
        const auto marker=reference.image(region);
        cv::Mat scores;cv::matchTemplate(ink,marker,scores,cv::TM_SQDIFF_NORMED);
        double ink_error;cv::minMaxLoc(scores,&ink_error);
        cv::matchTemplate(gray,marker,scores,cv::TM_CCOEFF_NORMED,reference.outline(region));
        double marker_outline=0;
        for(int y=0;y<scores.rows;++y)for(int x=0;x<scores.cols;++x)
            if(const auto score=scores.at<float>(y,x);std::isfinite(score))marker_outline=std::max(marker_outline,double(score));
        const double absence=minimum_quality+std::min({.45-strongest_outline,.45-marker_outline,ink_error-.75});
        return absence;
    }
    return -1000;
}
}

std::optional<unsigned> read_buff_stack(const DetectionInput& input,Rect cell,double scale,unsigned maximum,BuffStackEvidence evidence) {
    if(!input.frame.valid() || !std::isfinite(scale) || scale<.5 || scale>4 || !maximum || (input.cancelled && input.cancelled()))return {};
    auto sample=sample_caption(input.frame,cell,scale);
    if(!sample)return {};
    const auto& references=caption_references();
    std::vector<double> qualities(maximum+1,-1000);
    // Lower artwork is evidence for an uncaptioned icon, but can remain
    // partly visible behind a printed count. Calibrate that partial match
    // below a clear complete caption instead of comparing unlike raw scores.
    if(evidence.single_stack_similarity)
        qualities[1]=minimum_quality+(*evidence.single_stack_similarity-.5)/3;
    std::vector<std::vector<double>> styles(caption_fonts.size(),std::vector<double>(maximum+1,-1000));
    double strongest_outline=0,inconclusive=-1000;
    // All reference appearances enter one ranking. No buff-specific reader,
    // platform branch or second attempt can bypass its confidence margin.
    for(const auto& reference:references) {
        if(input.cancelled && input.cancelled())return {};
        if(reference.value>maximum)continue;
        const auto match=match_caption(reference,*sample);
        strongest_outline=std::max(strongest_outline,match.outline);
        styles[reference.style][reference.value]=std::max(styles[reference.style][reference.value],match.quality);
    }
    // Each reference style must separate its own digit candidates before
    // contributing a count. Inconclusive styles retain their plausible
    // alternatives; they are not evidence that the caption is absent.
    for(unsigned i=0;i<styles.size();++i) {
        const auto rank=rank_captions(styles[i]);
        // A strong calibrated match can stand alone. A clear raw match that
        // falls below that level needs another reference style to support the
        // same count; one weak rendering must not accept a partial caption.
        bool corroborated=rank.best>minimum_quality;
        if(rank.confident(caption_fonts[i].calibration) && !corroborated) {
            for(unsigned j=0;j<styles.size();++j)if(j!=i) {
                const auto other=rank_captions(styles[j]);
                const auto quality=styles[j][rank.value];
                if(quality+caption_fonts[j].calibration>minimum_quality &&
                    other.best-quality<=minimum_margin){corroborated=true;break;}
            }
        }
        if(rank.confident(caption_fonts[i].calibration) && corroborated)
            qualities[rank.value]=std::max(qualities[rank.value],rank.best);
        else inconclusive=std::max(inconclusive,rank.best);
    }
    if(evidence.uncaptioned_single)
        qualities[1]=std::max(qualities[1],uncaptioned_quality(*sample,references.front(),strongest_outline));
    // Apply the same admission gate to the no-caption candidate. An uncertain
    // absence score cannot outrank a caption that has passed its own checks.
    if(qualities[1]<=minimum_quality)qualities[1]=-1000;
    // Remove unsupported candidates before ranking, not after choosing a
    // winner. A weaker rendering cannot contradict a stronger rendering that
    // excludes that count, whether the stronger rendering is decisive or tied.
    for(unsigned value=2;value<qualities.size();++value)if(qualities[value]>-1000) {
        for(unsigned i=0;i<styles.size();++i) {
            const auto alternative=rank_captions(styles[i]);
            if(alternative.best>qualities[value] && alternative.best-styles[i][value]>minimum_margin) {
                qualities[value]=-1000;break;
            }
        }
    }
    // A readable multiplier disproves the no-caption interpretation, even
    // when part of the underlying artwork is still visible around its text.
    if(rank_captions(qualities,2).distinct())qualities[1]=-1000;
    auto rank=rank_captions(qualities);
    if(input.cancelled && input.cancelled())return {};
    if(rank.value==1)rank.second=std::max(rank.second,inconclusive);
    // Every remaining candidate already passed its quality gate. Compare only
    // calibrated ranking scores here; applying that gate again double-counts
    // the style penalty and rejects otherwise clear small captions.
    if(rank.distinct())return rank.value;
    return {};
}
}
