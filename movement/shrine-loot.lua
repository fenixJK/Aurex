local h=require("helpers")
return function(p)
    local legs={{7,"right","forward"},{10,"forward"},{10,"forward","right"},{7,"backward"},{2,"right"},{3.75,"backward"},{3,"left"}}
    for i=1,4 do h.append(legs,{{5,"left"},{1.5,"backward"},{5,"right"},{1.5,"backward"}}) end
    for i=1,2 do h.append(legs,{{15,"left"},{1,"forward"},{15,"right"},{1,"forward"}}) end
    legs[#legs+1]={15,"left"}
    for i=1,4 do h.append(legs,{{1.5,"forward"},{5,"right"},{1.5,"forward"},{5,"left"}}) end
    return {h.walk(legs)}
end
