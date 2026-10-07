#include "hive/modules/quests/detection.hpp"
#include "hive/game/detection/menus.hpp"
#include "hive/modules/quests/catalog.hpp"
#include "pixels.hpp"
#include <algorithm>
#include <tuple>

namespace hive::features::quests {
BrowserScene locate_browser(const game::DetectionInput& input,const std::vector<CardRegion>& regions) {
    BrowserScene result;
    if (!input.frame.valid() || input.text.size()>2048 || (input.cancelled && input.cancelled())) return result;
    double top=0;
    for (const auto& control : game::menu_controls(input))
        if (control.tab==game::MenuTab::quests) {
            result.toggle=control.center(); result.panel_visible=control.selected==true;
            top=control.bounds.y+control.bounds.height;
        }
    result.panel_visible=result.panel_visible || !regions.empty() || quest_panel_visible(input);
    std::optional<Rect> observed;
    if (!regions.empty() && !regions.front().rows.empty()) observed=regions.front().rows.front().area;
    else if (result.toggle) observed=quest_progress_area(input);
    result.panel_visible=result.panel_visible || observed.has_value();
    if (!observed || observed->y<top) return result;
    const auto area=*observed;
    for (const auto& card : regions) for (const auto& row : card.rows)
        if (std::abs(row.area.x-area.x)>.03 || std::abs(row.area.width-area.width)>.03) return result;
    result.scroll=Point{area.x+area.width*.5,area.y+area.height*.5};
    if (!regions.empty()) {
        for (const auto& card:regions) {
            const auto definition=resolve_quest(card.quest,card.giver);
            result.fingerprint+=std::to_string(int(card.title.y*input.frame.height/4))+":"+card.giver+":"+(definition?definition->name:card.quest)+"\n";
            for (const auto& row:card.rows)
                result.fingerprint+=std::to_string(int(row.area.y*input.frame.height/4))+":"+std::string("row")+"\n";
        }
        return result;
    }
    const int left=int(area.x*input.frame.width),width=int(area.width*input.frame.width);
    int start=-1,last=-1;
    const int gap=std::max(2,width/150);
    for (int y=int(top*input.frame.height);y<=input.frame.height;++y) {
        unsigned colored=0;
        if (y<input.frame.height) for (double x:{.025,.04,.1,.9,.96,.975}) {
            const auto px=std::clamp(left+int(width*x),0,input.frame.width-1);
            const auto offset=(std::size_t(y)*input.frame.width+px)*4;
            const auto* rgb=input.frame.rgba.data()+offset;
            colored+=progress_bar_color(rgb[0],rgb[1],rgb[2]);
        }
        if (colored>=4) { if (start<0) start=y; last=y; }
        if (start>=0 && (y-last>gap || y==input.frame.height)) {
            if (last-start>=gap*3) result.fingerprint+=std::to_string(start/4)+":row\n";
            start=-1;
        }
    }
    return result;
}
}
