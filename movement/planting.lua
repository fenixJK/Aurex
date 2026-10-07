local h=require("helpers")
local walk,turn=h.walk,h.turn
return function(p)
    local slot=p.slot
    local elevated=walk({{67.5,"backward","left"}})
    local upper=h.upper_field()
    local settle={kind="checkpoint",completion="movement",attempts=0,approach={{label="Alignment pause",keys={},ms=200}}}
    local routes={}
    routes["coconut"]=h.upper_gate(false,true)
    routes["pepper"]=h.upper_gate(true,true)
    routes["sunflower"]={walk({{14,"backward"}}),turn("rotate_right",1),walk({{25,"right"},{15,"forward"},{9,"backward"}}),turn("rotate_right",1)}
    routes["dandelion"]={turn("rotate_right",1),walk({{39,"backward"}}),turn("rotate_left",3),walk({{51,"forward"},{6,"right"},{9,"backward"}})}
    routes["mushroom"]={walk({{36,"backward","left"}}),turn("rotate_right",4),walk({{47,"forward"},{23,"right"},{3,"backward","left"}})}
    routes["blueflower"]={walk({{88.875,"backward","left"},{27,"left"}}),turn("rotate_left",2),walk({{15,"forward"},{20,"left"},{20,"forward"},{10,"backward"}})}
    routes["clover"]={walk({{44.75,"backward","left"},{52.5,"left"},{2.8,"backward","right"},{7.7,"backward"},{20.5,"left"},{5.5,"forward"},{20,"forward","left"},{5,"forward"},{10,"left"},{10,"forward","left"},{15,"right"},{15,"right","forward"},{3,"backward"}})}
    routes["spider"]={elevated,turn("rotate_right",4),walk({{37.5,"forward"},{38,"left","forward"},{9,"backward","right"}})}
    routes["strawberry"]={elevated,turn("rotate_right",4),walk({{31,"forward"},{7,"forward","left"},{30.25,"left"},{35,"forward","left"},{3,"backward","right"}})}
    routes["pineapple"]={elevated,turn("rotate_right",4),walk({{30,"forward"},{20,"forward","right"}}),turn("rotate_right",2),walk({{43.5,"forward"},{18,"right"},{6,"forward"}}),turn("rotate_left",2),walk({{66,"forward"},{19,"forward","left"},{7,"backward","right"}})}
    routes["bamboo"]={elevated,turn("rotate_right",4),walk({{23.5,"forward"},{31.5,"forward","right"},{10,"right"}}),turn("rotate_right",2),walk({{30,"forward"},{15,"left"},{3,"forward"},{1,"right"},{3,"backward"}})}
    routes["cactus"]={elevated,turn("rotate_right",4),upper,walk({{49.5,"forward"},{40,"right"},{5,"backward"},{3,"left"}})}
    routes["pinetree"]={elevated,turn("rotate_right",4),upper,walk({{45,"forward"},{3,"backward"},{3,"left"},{47,"left","forward"}}),turn("rotate_left",2),walk({{9,"right"},{9,"forward"},{3,"left"},{3,"backward"}})}
    routes["mountaintop"]={elevated,turn("rotate_right",4),upper,walk({{3.75,"right"},{85,"forward"},{45,"right"},{50,"backward"}}),walk({{60,"right"},{5,"left"},{7,"forward"},{9,"forward","left"},{16.5,"forward"}}),turn("rotate_right",4)}
    routes["rose"]={elevated,turn("rotate_right",4),upper,walk({{3.75,"right"},{38,"forward"}}),turn("rotate_left",4),walk({{14,"right"},{15,"forward","left"},{1,"backward"}}),settle,walk({{16,"right"},{49,"forward"}}),turn("rotate_left",4),walk({{10,"right"},{12,"right","forward"},{7,"backward","left"}}),turn("rotate_left",2)}
    routes["stump"]={elevated,turn("rotate_right",4),walk({{30,"forward"},{20,"forward","right"}}),turn("rotate_right",2),walk({{43.5,"forward"},{18,"right"},{6,"forward"}}),turn("rotate_left",2),walk({{43,"forward"},{30,"forward","right"},{50,"right"},{14,"backward","left"},{10,"left"}}),turn("rotate_right",2)}
    routes["pumpkin"]={elevated,turn("rotate_right",4),walk({{31,"forward"},{7.8,"left"},{10,"backward"},{5,"right"},{1.5,"forward"},{60,"left"},{3.75,"right"},{45,"forward"},{40,"right","forward"},{10,"right"},{3,"left"},{3,"backward"}})}
    local path=assert(routes[p.field],"Unknown planting field")
    table.insert(path,1,h.ramp(slot))
    return path
end
