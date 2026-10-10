local h=require("helpers")
local walk,turn=h.walk,h.turn
return function(p)
    local path={walk({{3,"forward"},{52,"left"},{3,"forward"}})}
    local ledge=h.motion()
    ledge:hold({"forward","jump"},300)
    ledge:walk(5,{"forward","right"})
    ledge:hold({"forward","jump"},300)
    ledge:hold({},500)
    path[#path+1]=ledge:finish()
    path[#path+1]=walk({{2,"forward"},{15,"right"},{6,"forward","right"},{7,"forward"},{5,"backward","left"},{23,"forward"},{12,"left"},{8,"left","forward"},{10,"forward"},{5,"right"},{25,"forward","right"},{25,"left"},{17,"backward"}})
    if not p.dispenser then path[#path+1]=walk({{12,"forward","right"},{4,"forward"}}) end
    if p.purchase then path[#path+1]=walk({{6,"left"},{10,"backward","left"}}) end
    return path
end
