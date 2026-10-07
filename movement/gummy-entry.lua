local h=require("helpers")
return function()
    local entry=h.motion()
    entry:hold({},500)
    entry:walk(6,{"forward"})
    return {entry:finish()}
end
