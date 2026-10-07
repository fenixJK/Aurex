local h=require("helpers")
return function(p)
    local path={}
    local vertical=p.start_forward>0 and "backward" or "forward"
    local horizontal=p.start_side<0 and "right" or "left"
    local reverse_vertical=vertical=="forward" and "backward" or "forward"
    local reverse_horizontal=horizontal=="left" and "right" or "left"
    local function move(key)
        local spacing=(key=="forward" or key=="backward") and p.forward_spacing or p.lateral_spacing
        path[#path+1]=h.walk({{spacing,key}})
    end
    for i=1,p.count do
        if i==2 then move(vertical)
        elseif i==3 then move(horizontal)
        elseif i==4 then move(reverse_vertical) end
        path[#path+1]={kind="marker",name="place"}
    end
    if p.count>=3 then move(reverse_horizontal) end
    if p.count==2 or p.count==3 then move(reverse_vertical) end
    return path
end
