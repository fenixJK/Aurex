#include "connection.hpp"
#include "dialog.hpp"
#include <cctype>
#include <set>

namespace hive::game {
Reading<Connection> read_connection(const Frame& frame,const std::vector<TextLine>& lines,std::uint64_t generation) {
    Reading<Connection> result{Knowledge::unknown,{},frame.captured,frame.window,generation};
    if(!frame.valid()) return result;
    bool honey=false,pollen=false,disconnected=false;
    for(const auto& line:lines) {
        const auto& r=line.pixels;
        if(!r.valid() || r.x<0 || r.y<0 || r.x+r.width>frame.width || r.y+r.height>frame.height
            || (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<.5f || *line.confidence>1))) continue;
        std::string name;
        for(unsigned char c:line.text) {
            if(std::isspace(c)) { if(!name.empty() && name.back()!=' ') name+=' '; }
            else name+=static_cast<char>(std::tolower(c));
        }
        if(!name.empty() && name.back()==' ') name.pop_back();
        const auto x=(r.x+r.width/2)/frame.width,y=(r.y+r.height/2)/frame.height;
        if(x>=.3 && x<=.7 && y>=.2 && y<=.8 && (name=="disconnected" || name=="connection error")) disconnected=true;
        if(y<.2) {
            honey=honey || name=="honey" || name.starts_with("honey ");
            pollen=pollen || name=="pollen" || name.starts_with("pollen ");
        }
    }
    if(disconnected) result={Knowledge::present,Connection::disconnected,frame.captured,frame.window,generation};
    else if(honey && pollen) result={Knowledge::present,Connection::connected,frame.captured,frame.window,generation};
    return result;
}
bool client_home_screen(const TextSnapshot& text) {
    if (text.width<300 || text.height<200) return false;
    std::set<std::string> labels;
    for (const auto& line:text.lines) {
        const auto& r=line.pixels;
        if (!r.valid() || r.x<0 || r.y<0 || r.x+r.width>text.width || r.y+r.height>text.height
            || (line.confidence && (!std::isfinite(*line.confidence) || *line.confidence<.65f))) continue;
        std::string name;
        for (unsigned char c:line.text) if (!std::isspace(c)) name+=static_cast<char>(std::tolower(c));
        if (name=="home" || name=="charts" || name=="avatar" || name=="search") labels.insert(std::move(name));
    }
    return labels.contains("home") && labels.size()>=3;
}
Observation<Point> read_reconnect(const Context& context,const Reading<TextSnapshot>& text) {
    const PromptLandmark heading{{"Disconnected","Connection Error"},{.2,.2,.6,.65},.8f};
    const auto right=read_dialog(context,text,heading,{"Leave","Reconnect"});
    if (right.has_value(context,Time(2000)))
        return {Knowledge::present,right.value.no,right.captured,right.window,right.generation};
    const auto left=read_dialog(context,text,heading,{"Reconnect","Leave"});
    if (left.has_value(context,Time(2000)))
        return {Knowledge::present,left.value.yes,left.captured,left.window,left.generation};
    return {};
}
}
