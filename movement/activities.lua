local h=require("helpers")
local fields=require("fields")
local walk,turn=h.walk,h.turn
return function(p)
    local name,slot=p.kind,p.slot
    if name=="coconut" then
        local path={h.ramp(slot)}
        local base=h.upper_gate(false,false)
        table.remove(base)
        local climb=base[#base]
        climb.steps[#climb.steps]={4,"forward"}
        h.append(path,base)
        path[#path+1]=turn("rotate_left",1)
        path[#path+1]=walk({{30,"forward"}})
        path[#path+1]=turn("rotate_right",1)
        path[#path+1]=walk({{15.7,"left"},{8,"forward"}})
        return path
    end
    if name=="mountain-booster" or name=="sticker-printer" then
        local path
        if p.cannon then
            local f=h.flight(slot); f:hold({},name=="sticker-printer" and 4000 or 3000); path=f:finish()
        else path=fields({slot=slot,field="mountaintop"}) end
        if name=="sticker-printer" then
            if not p.cannon then path[#path+1]=walk({{4.5,"backward"}}) end
            path[#path+1]=walk({{31,"left"},{3,"backward"}})
        else path[#path+1]=p.cannon and walk({{40.5,"right"}}) or walk({{27,"right"},{18,"backward"},{27,"right"}}) end
        return path
    end
    if name=="honeystorm" then
        if not p.cannon then return {h.ramp(slot),walk({{44.75,"backward","left"},{52.5,"left"},{2.8,"backward","right"},{6.7,"backward"},{40.5,"left"},{5,"backward"}}),turn("rotate_right")} end
        local f=h.flight(slot); f:hold({"forward","left"},1180); f:jump(2); f:hold({"forward","left"},5000); f:jump(); f:hold({},1500)
        f:walk({{10,"forward","left"},{10,"backward"},{7,"backward","right"},{9,"backward"}}); f:turn("rotate_right",2); return f:finish()
    end
    if name=="robo-pass" then
        local path
        if p.cannon then
            local f=h.flight(slot); f:hold({"left","backward"},1400); f:jump(2); f:hold({"left","backward"},1100); f:hold({"backward"},650)
            f:jump(); f:hold({},1500); f:turn("rotate_right",4); f:walk({{4,"right","forward"},{23,"forward"},{9,"left"}}); path=f:finish()
        else
            path={h.ramp(slot),walk({{67.5,"backward","left"}}),turn("rotate_right",4),h.upper_field(),walk({{49.5,"forward"},{3.375,"left"},{36,"forward"},{54,"right"},{54,"backward"}}),walk({{58.5,"right"},{3,"left"},{57,"forward"},{16,"left"}})}
        end
        path[#path+1]=walk({{3,"forward"},{8,"left"},{2,"right"},{13,"forward"}}); path[#path+1]=turn("rotate_left",2)
        local entrance=h.motion(); entrance:walk(1.5,{"forward"}); entrance:hold({"jump"},100); entrance:walk(8.5,{"forward"}); entrance:walk(3,{"left"}); entrance:walk(20,{"forward"})
        path[#path+1]=entrance:finish(); return path
    end
    if name=="glue" then
        if p.cannon then
            local f=h.flight(slot); f:hold({"forward"},1170); f:jump(2); f:hold({},6750); f:walk({{18,"forward"},{8.5,"left"},{3,"left","forward"}}); return f:finish()
        end
        local path={walk({{3,"forward"},{52,"left"},{3,"forward"}})}
        local step=h.motion(); step:hold({"forward","jump"},300); step:walk(5,{"right"}); step:hold({"forward","jump"},300); step:hold({},500)
        path[#path+1]=step:finish()
        path[#path+1]=walk({{2,"forward"},{15,"right"},{6,"forward","right"},{7,"forward"},{5,"backward","left"},{23,"forward"},{12,"left"},{8,"left","forward"},{10,"forward"},{5,"right"},{25,"forward","right"},{50,"left"},{2,"right"},{40,"forward"}})
        path[#path+1]=turn("rotate_right",2); path[#path+1]=walk({{55,"forward"},{10,"left"}})
        path[#path+1]=turn("rotate_right",2); path[#path+1]=walk({{5.79,"forward","right"},{50,"forward"}})
        local ledge=h.motion(); ledge:hold({"jump"},300); ledge:walk(6,{"forward"}); ledge:hold({"jump"},100); ledge:walk(4,{"forward","right"})
        path[#path+1]=ledge:finish(); path[#path+1]=turn("rotate_left",4); return path
    end
    error("Unknown activity route")
end
