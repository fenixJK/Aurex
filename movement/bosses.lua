local h=require("helpers")
local fields=require("fields")
local walk,turn=h.walk,h.turn
return function(p)
    if p.enemy=="King Beetle" then
        local path=fields({slot=p.slot,field="blueflower"})
        path[#path+1]=walk({{35,"right","forward"},{28,"forward"}})
        path[#path+1]=turn("rotate_right",2)
        local ledge=h.motion(); ledge:walk(5,{"right"}); ledge:hold({"jump"},200); ledge:walk(3,{"right"}); ledge:walk(5,{"right","forward"})
        path[#path+1]=ledge:finish(); return path
    end
    if p.enemy=="Tunnel Bear" then
        local path={h.ramp(p.slot),walk({{67.5,"backward","left"}}),turn("rotate_right",4),walk({{23.5,"forward"},{31.5,"forward","right"},{10,"right"}}),turn("rotate_right",2),walk({{28,"forward"},{13,"left"},{25,"right"}})}
        local bridge=h.motion(); bridge:walk(12,{"forward"},true); path[#path+1]=bridge:finish()
        path[#path+1]=turn("rotate_left",2)
        path[#path+1]=walk({{35,"forward"},{25,"right"},{12,"forward"},{3.5,"right"}})
        path[#path+1]=turn("rotate_right",4)
        path[#path+1]=walk({{37,"forward"},{10,"forward","left"},{5,"backward"},{15,"right"},{10,"backward"}})
        local drop=h.motion(); drop:hold({},1500); path[#path+1]=drop:finish()
        path[#path+1]=turn("rotate_right",2)
        path[#path+1]=walk({{42,"backward"},{3.85,"left"}})
        return path
    end
    if p.enemy=="Commando Chick" then
        local path={h.ramp(p.slot),walk({{44.75,"backward","left"},{42.5,"left"},{8.5,"backward"},{22.5,"left"}}),turn("rotate_left",2),walk({{27,"forward"},{12,"left","forward"},{11,"forward"},{10,"left"},{6,"right"},{2,"left"},{7,"forward"}})}
        local platform=h.motion(); platform:hold({},750)
        for _,distance in ipairs({5.5,6,6}) do platform:hold({"jump"},50); platform:walk(distance,{"forward"}); platform:hold({},750) end
        platform:walk(5,{"forward"}); platform:walk(9,{"backward"}); platform:hold({},1000); platform:hold({"jump"},50); platform:walk(.5,{"backward"})
        path[#path+1]=platform:finish(); return path
    end
    error("Unknown boss approach")
end
