local h=require("helpers")
return function(p)
    return {h.walk({{9,"backward"},{3,"right","forward"},{1,"forward"}})}
end
