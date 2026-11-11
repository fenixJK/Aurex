#include "buff_vision.hpp"
#include "buff_stack.hpp"
#include <algorithm>

namespace hive::game::detail {
namespace {
std::array<int,3> totals(const Patch& patch,unsigned count) {
    std::array<int,3> result{};
    for(std::size_t i=0;i<count*3;i+=3) for(int c=0;c<3;++c) result[c]+=patch[i+c];
    return result;
}
std::array<int,3> totals(const BuffIcon& icon) {
    std::array<int,3> result{};
    for(unsigned i=0;i<icon.columns*icon.rows;++i) for(int c=0;c<3;++c) result[c]+=(icon.pixels[i]>>(16-c*8))&255;
    return result;
}
bool color_matches(const Frame& frame,double left,double top,double scale,const BuffColorCheck& check) {
    const int x0=std::lround(left+check.region.x*scale),y0=std::lround(top+check.region.y*scale);
    const int x1=std::max(x0+1,int(std::lround(left+(check.region.x+check.region.width)*scale)));
    const int y1=std::max(y0+1,int(std::lround(top+(check.region.y+check.region.height)*scale)));
    if(x0<0 || y0<0 || x1>frame.width || y1>frame.height) return false;
    std::array<double,3> error{};
    for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) {
        const auto offset=(std::size_t(y)*frame.width+x)*4;
        for(int c=0;c<3;++c)error[c]+=std::abs(int(frame.rgba[offset+c])-check.color[c]);
    }
    for(int c=0;c<3;++c)if(error[c]>check.tolerance[c]*(x1-x0)*(y1-y0))return false;
    return true;
}
double score(const Patch& patch,const std::array<int,3>& actual,const BuffAppearance& appearance,const std::array<int,3>& reference,double scale) {
    const auto& icon=*appearance.icon;
    if(std::any_of(icon.coverage.begin(),icon.coverage.end(),[](auto value){return value!=0;})) {
        // The timer replaces only the tile background. Fit that background per
        // row, then compare the opaque artwork and its boundary. Comparing the
        // whole tile's RGB would reject an unchanged symbol as its timer drains.
        double error=0,weight=0,contrast=0,foreground=0;
        std::array<double,3> foreground_color{};double cores=0,core_error=0,negative_distance=0,negatives=0,shift=0;
        for(unsigned i=0;i<icon.columns*icon.rows;++i)if(icon.coverage[i]>230) {
            ++cores;
            for(int c=0;c<3;++c) {
                const double color=(icon.pixels[i]>>(16-c*8))&255;
                foreground_color[c]+=color;
                shift+=patch[i*3+c]-color;
            }
        }
        if(cores<4)return 255;
        shift=std::clamp(shift/(cores*3),-25.,25.);
        const auto foreground_pixel=[&](unsigned i,int c){return std::clamp(double((icon.pixels[i]>>(16-c*8))&255)+shift,0.,255.);};
        for(unsigned i=0;i<icon.columns*icon.rows;++i)if(icon.coverage[i]>230)
            for(int c=0;c<3;++c)core_error+=std::abs(patch[i*3+c]-foreground_pixel(i,c));
        for(auto& c:foreground_color)c=std::clamp(c/cores+shift,0.,255.);
        core_error/=cores*3;
        if(core_error>32)return 255;
        for(unsigned i=0;i<icon.columns*icon.rows;++i)if(icon.coverage[i]<13) {
            ++negatives;
            for(int c=0;c<3;++c)negative_distance+=std::abs(patch[i*3+c]-foreground_color[c]);
        }
        if(negatives<4 || negative_distance/(negatives*3)<core_error+6)return 255;
        std::array<double,288> expected_pixels{};
        for(unsigned y=0;y<icon.rows;++y) {
            std::array<double,3> background{};double denominator=0;
            for(unsigned x=0;x<icon.columns;++x) {
                const auto i=y*icon.columns+x;const double alpha=icon.coverage[i]/255.,inverse=1-alpha;
                denominator+=inverse*inverse;
                for(int c=0;c<3;++c)background[c]+=inverse*(patch[i*3+c]-alpha*foreground_pixel(i,c));
            }
            for(auto& channel:background)channel=std::clamp(channel/std::max(.01,denominator),0.,255.);
            for(unsigned x=0;x<icon.columns;++x) {
                const auto i=y*icon.columns+x;const double alpha=icon.coverage[i]/255.,w=alpha*alpha;
                for(int c=0;c<3;++c) {
                    const double color=foreground_pixel(i,c);
                    const double expected=alpha*color+(1-alpha)*background[c];
                    expected_pixels[i*3+c]=expected;
                    error+=w*std::abs(patch[i*3+c]-expected);
                    contrast+=alpha*std::abs(color-background[c]);
                }
                weight+=3*w;foreground+=3*alpha;
            }
        }
        // Compare horizontal symbol edges against the fitted background. The
        // timer's horizontal boundary is excluded, and drained rows may have
        // much less contrast than the original full-color reference.
        const auto edge_similarity=[&] {
            double dot=0,actual=0,reference=0;
            for(unsigned y=0;y<icon.rows;++y)for(unsigned x=1;x+1<icon.columns;++x) {
                const auto l=y*icon.columns+x-1,r=l+2;double a=0,b=0;
                for(int c=0;c<3;++c) {
                    const double delta=int(patch[r*3+c])-int(patch[l*3+c]);a+=delta*delta;
                    const double d=expected_pixels[r*3+c]-expected_pixels[l*3+c];b+=d*d;
                }
                dot+=std::sqrt(a*b);actual+=a;reference+=b;
            }
            return actual>=1 && reference>=1?dot/std::sqrt(actual*reference):0.;
        };
        double similarity=edge_similarity();
        // A textured background can have edges of its own. When the opaque
        // artwork already agrees strongly, estimate the scenery locally from
        // surrounding uncovered pixels instead of treating each row as flat.
        // Retain the original foreground error/contrast checks: the more flexible
        // background model must not explain away mismatching artwork.
        // Binary masks lack the boundary coverage needed to distinguish those
        // scenery edges, so this fallback requires an antialiased silhouette.
        if(similarity<appearance.minimum_edge_similarity
            && similarity>=appearance.minimum_edge_similarity-.12 && core_error<6
            && std::count_if(icon.coverage.begin(),icon.coverage.end(),[](auto alpha){return alpha>13 && alpha<230;})>=8
            && foreground>0 && error/std::max(1.,weight)<.2*contrast/foreground) {
            for(unsigned y=0;y<icon.rows;++y)for(unsigned x=0;x<icon.columns;++x) {
                std::array<double,3> background{};double denominator=0;
                for(unsigned j=0;j<icon.columns;++j) {
                    const auto i=y*icon.columns+j;
                    const double alpha=icon.coverage[i]/255.,inverse=1-alpha;
                    const double distance=int(x)-int(j),proximity=1/(1+distance*distance);
                    denominator+=proximity*inverse*inverse;
                    for(int c=0;c<3;++c)background[c]+=proximity*inverse*(patch[i*3+c]-alpha*foreground_pixel(i,c));
                }
                if(denominator<.01)continue;
                const auto i=y*icon.columns+x;const double alpha=icon.coverage[i]/255.;
                for(int c=0;c<3;++c)expected_pixels[i*3+c]=alpha*foreground_pixel(i,c)
                    +(1-alpha)*std::clamp(background[c]/denominator,0.,255.);
            }
            similarity=edge_similarity();
        }
        if(similarity<appearance.minimum_edge_similarity)return 255;
        // Uniform scenery can match an artwork color but cannot establish its
        // silhouette. Keep such observations unknown instead of accepting them.
        if(foreground<1 || contrast/foreground<8 || error/std::max(1.,weight)>appearance.maximum_noise_ratio*contrast/foreground)return 255;
        // Strong silhouettes tolerate compression/color error; weak silhouettes
        // must agree closely with the opaque artwork to avoid matching scenery.
        const double limit=12+120*std::clamp(similarity-.8,0.,.1);
        return 18*error/(std::max(1.,weight)*limit);
    }
    double minimum_error=0;
    for(int c=0;c<3;++c) {
        const auto error=std::abs(actual[c]-reference[c]);
        if(error>appearance.icon->columns*appearance.icon->rows*appearance.mean_color_limit)return 255;
        minimum_error+=error;
    }
    if(minimum_error>appearance.icon->columns*appearance.icon->rows*3*appearance.color_limit) return 255;
    if(appearance.pixel_error_limit<255) {
        const auto count=appearance.icon->columns*appearance.icon->rows;
        const double shift=appearance.normalization==BuffNormalization::brightness
            ?std::clamp((actual[0]+actual[1]+actual[2]-reference[0]-reference[1]-reference[2])/(count*3.),-25.,25.):0;
        const auto limit=appearance.pixel_error_limit+(scale>=1.5?appearance.upscaled_pixel_slack:0);
        for(unsigned i=0;i<count;++i) for(int c=0;c<3;++c)
            if(std::abs(patch[i*3+c]-std::clamp(double((appearance.icon->pixels[i]>>(16-c*8))&255)+shift,0.,255.))>limit)return 255;
    }
    return difference(patch,*appearance.icon,actual,reference,appearance.color_limit,appearance.normalization==BuffNormalization::rows,appearance.normalization==BuffNormalization::brightness);
}
std::optional<double> meter_level(const Frame& frame,Rect cell,double scale,const BuffMeter& meter) {
    const auto match=[&](int x,int y) {
        if(x<0 || y<0 || x>=frame.width || y>=frame.height) return false;
        const auto offset=(std::size_t(y)*frame.width+x)*4;
        for(int c=0;c<3;++c) if(std::abs(int(frame.rgba[offset+c])-meter.color[c])>24) return false;
        return true;
    };
    const int bottom=std::lround(cell.y+cell.height)-1,top=std::lround(cell.y);
    std::array<int,2> heights{};
    for(int side=0;side<2;++side) {
        const int edge=std::lround(side?cell.x+cell.width-2*scale:cell.x+scale);
        for(int dx=-1;dx<=1;++dx) {
            int gaps=0,height=0;
            for(int y=bottom;y>=top;--y) {
                if(match(edge+dx,y)) {height=bottom-y+1;gaps=0;}
                else if(++gaps>std::max(1,int(std::ceil(scale))))break;
            }
            heights[side]=std::max(heights[side],height);
        }
    }
    if(!heights[0] || !heights[1] || std::abs(heights[0]-heights[1])>3*scale) return {};
    return meter.minimum+meter.range*std::clamp(std::min(heights[0],heights[1])/cell.height,0.,1.);
}
}
BuffScan scan_buffs(const DetectionInput& input,const BuffBar& layout,std::span<const BuffDefinition> definitions) {
    BuffScan result;result.ambiguous.resize(definitions.size());
    if(!input.frame.valid() || !input.generation || definitions.empty() || !std::isfinite(layout.scale)
        || layout.scale<.5 || layout.scale>4 || !std::isfinite(layout.pitch) || layout.pitch<15 || layout.pitch>160
        || !std::isfinite(layout.origin) || !std::isfinite(layout.bottom) || layout.bottom>input.frame.height || layout.bottom<layout.pitch) return result;
    struct Prepared {std::size_t definition,region;const BuffAppearance* appearance;std::array<int,3> reference;};
    struct Region {Rect area;unsigned columns,rows;bool operator==(const Region&) const=default;};
    std::vector<Region> regions;std::vector<Prepared> prepared;
    int first=0,last=0;
    for(std::size_t i=0;i<definitions.size();++i) for(const auto& a:definitions[i].appearances) {
        if(!a.icon || !a.icon->columns || !a.icon->rows || a.icon->rows>8 || a.icon->columns>96/a.icon->rows) return result;
        const Region key{a.region,a.icon->columns,a.icon->rows};
        auto region=std::find(regions.begin(),regions.end(),key);
        const auto index=std::size_t(region-regions.begin());if(region==regions.end())regions.push_back(key);
        prepared.push_back({i,index,&a,totals(*a.icon)});first=std::min(first,a.first_offset);last=std::max(last,a.last_offset);
    }
    struct Sample {Patch pixels;std::array<int,3> totals;};
    std::vector<std::optional<Sample>> patches(regions.size());
    std::vector<double> errors(definitions.size()),margins(definitions.size());
    std::vector<unsigned> specificity(definitions.size());
    for(int dy=first;dy<=last;++dy) {
        const double top=layout.bottom-layout.pitch+dy*layout.scale;
        for(double left=std::max(0.,layout.origin);left+layout.pitch<=input.frame.width;left+=1) {
            if(input.cancelled && input.cancelled()) return {};
            if(layout.aligned) {
                const double slot=(left-layout.origin)/layout.pitch;
                const double tolerance=3*layout.scale+.005*(left-layout.origin);
                if(std::abs(slot-std::round(slot))*layout.pitch>tolerance)continue;
            }
            std::fill(patches.begin(),patches.end(),std::nullopt);std::fill(errors.begin(),errors.end(),255);std::fill(specificity.begin(),specificity.end(),0);
            for(const auto& p:prepared) {
                const auto& a=*p.appearance;
                if(dy<a.first_offset || dy>a.last_offset)continue;
                if(!std::all_of(a.color_checks.begin(),a.color_checks.end(),[&](const auto& check){return color_matches(input.frame,left,top,layout.scale,check);}))continue;
                auto& patch=patches[p.region];
                if(!patch) if(auto pixels=sample(input.frame,left,top,layout.scale,a.region,a.icon->columns,a.icon->rows))patch=Sample{*pixels,totals(*pixels,a.icon->columns*a.icon->rows)};
                if(!patch)continue;
                const auto error=score(patch->pixels,patch->totals,a,p.reference,layout.scale);
                if(error<a.icon->maximum_error && (a.specificity>specificity[p.definition] || (a.specificity==specificity[p.definition] && error<errors[p.definition]))) {
                    errors[p.definition]=error;specificity[p.definition]=a.specificity;margins[p.definition]=a.minimum_margin;
                }
            }
            const auto rank=*std::max_element(specificity.begin(),specificity.end());
            for(std::size_t i=0;i<errors.size();++i)if(specificity[i]<rank)errors[i]=255;
            const auto best=std::min_element(errors.begin(),errors.end());const auto selected=std::size_t(best-errors.begin());
            if(*best>=255)continue;
            const double error=*best;*best=255;const auto next=std::min_element(errors.begin(),errors.end());
            if(*next-error<=margins[selected]) {
                result.ambiguous[selected]=true;result.ambiguous[next-errors.begin()]=true;continue;
            }
            const Rect cell{left,top,layout.pitch,layout.pitch};
            auto previous=std::find_if(result.matches.begin(),result.matches.end(),[&](const auto& m){return std::abs(m.cell.x-left)<layout.pitch*.7;});
            if(previous==result.matches.end())result.matches.push_back({selected,cell,error,rank});
            else if(rank>previous->specificity || (rank==previous->specificity && error<previous->error))*previous={selected,cell,error,rank};
        }
    }
    for(const auto& m:result.matches)result.ambiguous[m.definition]=false;
    result.complete=true;return result;
}
BuffState read_buff_value(const DetectionInput& input,const BuffBar& layout,const BuffDefinition& definition,Rect cell) {
    BuffState result{std::string(definition.type.name),true};
    if(definition.value_kind==BuffValueKind::presence) {result.stacks=1;return result;}
    if(definition.value_kind==BuffValueKind::meter) {
        if(definition.meter)result.value=meter_level(input.frame,cell,layout.scale,*definition.meter);
        return result;
    }
    BuffStackEvidence evidence{definition.uncaptioned_single,{}};
    if(definition.single_stack) {
        auto a=*definition.single_stack;
        a.minimum_edge_similarity=.62;
        const auto reference=totals(*a.icon);
        // The lower artwork can establish a single stack even when its white
        // strokes resemble lettering. It competes with the printed counts in
        // the same reader; a failed caption read does not default to one.
        // Fit small artwork-size differences around the cell center as well
        // as position; HUD rounding need not preserve the source art's size.
        for(double artwork_scale:{.95,1.,1.05}) for(int dy=-2;dy<=2;++dy) for(int dx=-2;dx<=2;++dx) {
            const double inset=19*(1-artwork_scale);
            if(const auto patch=sample(input.frame,cell.x+(dx+inset)*layout.scale,cell.y+(dy+inset)*layout.scale,layout.scale*artwork_scale,a.region,a.icon->columns,a.icon->rows)) {
                const double error=score(*patch,totals(*patch,a.icon->columns*a.icon->rows),a,reference,layout.scale);
                if(error<a.icon->maximum_error)evidence.single_stack_similarity=std::max(evidence.single_stack_similarity.value_or(0.),1-error/(2*a.icon->maximum_error));
            }
        }
    }
    result.stacks=read_buff_stack(input,cell,layout.scale,definition.type.maximum,evidence);
    result.value=result.stacks;return result;
}
BuffState read_buff_candidates(const DetectionInput& input,const BuffBar& layout,const BuffDefinition& definition,
    std::span<const BuffMatch> matches,std::size_t definition_index) {
    BuffState result{std::string(definition.type.name),true};
    unsigned strength=0;bool conflict=false;
    for(const auto& match:matches) {
        if(match.definition!=definition_index)continue;
        const auto candidate=read_buff_value(input,layout,definition,match.cell);
        // A one-stack icon has no printed multiplier. A confidently matched
        // caption supplies more evidence than inferring one from its absence.
        // This does not order numeric values: conflicting captions stay unknown.
        const unsigned evidence=definition.value_kind==BuffValueKind::stacks && candidate.stacks
            ?(*candidate.stacks>1?2:1):(candidate.value || candidate.stacks?1:0);
        if(evidence>strength) {result=candidate;strength=evidence;conflict=false;}
        else if(evidence && evidence==strength
            && (candidate.stacks!=result.stacks || candidate.value!=result.value))conflict=true;
    }
    if(conflict || (input.cancelled && input.cancelled())) {result.stacks.reset();result.value.reset();}
    return result;
}
}
