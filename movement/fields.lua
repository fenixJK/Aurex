local h=require("helpers")
local walk,turn=h.walk,h.turn
return function(p)
    local slot=p.slot
    local function ramp() return h.ramp(slot) end
    local elevated=walk({{67.5,"backward","left"}})
    local upper=h.upper_field()
    local settle=h.motion(); settle:hold({},200)
    local routes={}
    routes["dandelion"]={ramp(),walk({{39,"backward","left"},{14,"left"}}),turn("rotate_left")}
    routes["mushroom"]={ramp(),walk({{36,"backward","left"}}),turn("rotate_right",4),walk({{31.5,"forward"}})}
    routes["clover"]={ramp(),walk({{44.75,"backward","left"},{52.5,"left"},{2.8,"backward","right"},{7.7,"backward"}}),walk({{20.5,"left"},{5.5,"forward"}})}
    routes["spider"]={ramp(),walk({{67.5,"backward","left"}}),turn("rotate_right",4),walk({{31.5,"forward"},{13,"forward","left"}})}
    routes["strawberry"]={ramp(),walk({{67.5,"backward","left"}}),turn("rotate_right",4),walk({{31,"forward"},{7,"forward","left"},{33.25,"left"},{6.75,"forward","left"}}),turn("rotate_left")}
    routes["cactus"]={ramp(),elevated,turn("rotate_right",4),upper,walk({{49.5,"forward"}}),turn("rotate_right",4),walk({{13.5,"left"}})}
    routes["pumpkin"]={ramp(),elevated,turn("rotate_right",4),upper,walk({{3.75,"right"},{38,"forward"},{18,"right","forward"},{10,"forward"}})}
    routes["pinetree"]={ramp(),elevated,turn("rotate_right",4),upper,walk({{3.75,"right"},{38,"forward"},{33,"left","forward"}})}
    routes["rose"]={ramp(),elevated,turn("rotate_right",4),upper,walk({{3.75,"right"},{38,"forward"}}),turn("rotate_left",4),walk({{14,"right"},{15,"forward","left"},{1,"backward"}}),settle:finish(),walk({{16,"right"},{49,"forward"}}),turn("rotate_right",2)}
    routes["pineapple"]={ramp(),elevated,turn("rotate_right",4),walk({{30,"forward"},{20,"forward","right"}}),turn("rotate_right",2),walk({{43.5,"forward"},{18,"right"},{6,"forward"}}),turn("rotate_left",2),walk({{65.5,"forward"},{1.5,"right"}})}
    routes["mountaintop"]={ramp(),elevated,turn("rotate_right",4),upper,walk({{3.75,"right"},{85,"forward"},{45,"right"},{50,"backward"}}),walk({{60,"right"},{15.75,"forward","left"},{13.5,"forward"}}),turn("rotate_right",4)}
    routes["stump"]={ramp(),elevated,turn("rotate_right",4),walk({{30,"forward"},{20,"forward","right"}}),turn("rotate_right",2),walk({{43.5,"forward"},{18,"right"},{6,"forward"}}),turn("rotate_left",2),walk({{43,"forward"},{30,"forward","right"},{24,"right"},{10,"backward"}}),turn("rotate_right",2)}
    routes["blueflower"]={ramp(),walk({{86.875,"backward","left"}})}
    h.append(routes["blueflower"],h.scope({walk({{28,"left"}}),turn("rotate_left")},"blueflower.entry",1))
    routes["bamboo"]={ramp(),walk({{67.5,"backward","left"}}),turn("rotate_right",4),walk({{23.5,"forward"},{31.5,"forward","right"},{10,"right"}}),turn("rotate_right",2)}
    routes.sunflower={walk({{5,"forward"},{9.2*slot-4,"right"},{9,"backward"},{6.75,"backward","right"}}),turn("rotate_right"),walk({{29,"right"}})}
    if p.field=="coconut" or p.field=="pepper" then
        local path=h.upper_gate(p.field=="pepper",false); table.insert(path,1,ramp()); return h.scope(path,p.field..".entry")
    end
    return h.scope(assert(routes[p.field],"Unknown field"),p.field..".entry")
end
