local h=require("helpers")
local walk,turn=h.walk,h.turn
local function blue_entrance()
    local entrance=h.motion()
    entrance:hold({"jump"},100)
    entrance:walk(6,{"forward"})
    entrance:walk(5,{"right"})
    entrance:walk(9,{"right","backward"})
    entrance:walk(4,{"right"})
    entrance:walk(2,{"left"})
    entrance:walk(21,{"backward"})
    entrance:walk(3.4,{"forward","left"})
    entrance:walk(16,{"left"})
    return entrance:finish()
end
return function(p)
    local slot,blue=p.slot,p.blue
    if p.cannon then
        local path={}
        if blue then
            local flight=h.blue_hq(slot)
            flight:walk({{4,"backward","left"},{8,"forward","left"},{6,"forward"},{5,"backward"},{8,"right"},{30,"forward"}})
            path = flight:finish()
            path[#path+1]=blue_entrance()
        else
            path[#path+1]=walk({{5,"forward"},{9.2 * slot - 4,"right"}})
            local ramp=h.motion()
            ramp:hold({"right","jump"},100)
            ramp:walk(2,{"right"})
            ramp:walk(1.8,{"right","forward"})
            ramp:walk(30,{"right"})
            ramp:hold({"right","jump"},300)
            ramp:walk(4,{"right","forward"})
            ramp:walk(3,{"right"})
            path[#path+1]=ramp:finish()
            path[#path+1]=turn("rotate_right",2)
            local entrance=h.motion()
            entrance:hold({"jump"},100)
            entrance:walk(3,{"forward"})
            entrance:hold({},1000)
            entrance:hold({"right","jump"},100)
            entrance:hold({"right"},300)
            entrance:hold({"jump"},50)
            entrance:hold({},1000)
            entrance:walk(8,{"forward","right"})
            entrance:walk(1,{"forward"})
            entrance:walk(6.75,{"right"})
            entrance:hold({},1000)
            path[#path+1]=entrance:finish()
            path[#path+1]=turn("rotate_right",4)
            path[#path+1]=walk({{9,"forward"},{3,"forward","left"},{5,"forward","right"}})
        end
        return path
    else
        local path={walk({{5,"forward"},{9.2 * slot - 4,"right"}})}
        if blue then
            path[#path+1]=walk({{88.875,"backward","left"},{27,"left"}})
            path[#path+1]=turn("rotate_left",2)
            path[#path+1]=walk({{50,"forward"}})
            path[#path+1]=blue_entrance()
        else
            path[#path+1]=walk({{67.5,"backward","left"}})
            path[#path+1]=turn("rotate_right",4)
            path[#path+1]=walk({{31.5,"forward"},{9,"left"},{9,"backward"},{58.5,"left"},{49.5,"forward"},{20.25,"left"}})
            path[#path+1]=turn("rotate_right",4)
            path[#path+1]=walk({{60.75,"forward"}})
            path[#path+1]=turn("rotate_right",2)
            path[#path+1]=walk({{9,"backward"},{15.75,"backward","right"},{29.7,"left"},{11.25,"forward"},{13.5,"left"}})
        end
        return path
    end
end
