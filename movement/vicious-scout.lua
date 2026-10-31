-- Viewing routes adapted from the user's scripts.7z / ServerHop.ahk.
-- The printer variant is complete; its unfinished tunnel variant is omitted.
-- Distances are tiles; airborne waits are milliseconds. Combat is handled by Aurex.
local h=require("helpers")
return function(p)
    local path,m,held={},h.motion(),{}
    local F,B,L,R="forward","backward","left","right"
    local function flush()
        if #m.steps>0 then path[#path+1]=m:finish();m=h.motion() end
    end
    local function hold(ms,extra)
        local keys={};for _,k in ipairs(held) do keys[#keys+1]=k end
        if extra then keys[#keys+1]=extra end
        m:hold(keys,ms)
    end
    local function move(n) assert(#held>0 and #held<=2);m:walk(n,held) end
    local function walk(n,a,b) held=b and {a,b} or {a};move(n);held={} end
    -- The source uses SendEvent's default 10 ms delay after each down/up.
    -- Include that delay explicitly; our input runtime adds no implicit sleep.
    local function jump(ms) hold(110,"jump");hold(ms+10) end
    local function glide(ms)
        jump(150);jump(ms)
    end
    local function tap_jump(ms) m.steps[#m.steps+1]={keys=held,ms=1,tap="jump"};hold(ms) end
    local function turn(k,n)
        for i=1,n or 1 do
            m.steps[#m.steps+1]={keys=held,ms=20,tap=k};hold(20)
        end
    end
    local function camera(k,n)
        assert(#held==0);flush()
        while n>0 do
            local count=math.min(8,n)
            path[#path+1]={kind="turn",key=k,count=count,press=50,release=20}
            n=n-count
        end
    end
    -- Each circuit leg inherits the preceding leg's pitch. Keep this local
    -- to the route invocation; a new hunt must not inherit old camera state.
    local previous={spawn={4,false},pepper={6,false},mountaintop={6,true},spider={6,false},cactus={6,false}}
    local pose=previous[p.from]
    local function pitch(n,down)
        down=not not down
        if not pose or pose[2]~=down then
            camera(down and "pitch_down" or "pitch_up",13)
            camera(down and "pitch_up" or "pitch_down",down and 7 or 4)
            pose={4,down}
        end
        local delta=n-pose[1]
        camera(delta<0 and "pitch_up" or "pitch_down",math.abs(delta))
        pose[1]=n
    end
    local function zoom(n) camera("zoom_out",n) end
    local function scout_camera(zoomed)
        -- FixRot in the source establishes pitch/zoom before spawnToPepper
        -- enables shift lock. Keep that order, including a neutral pointer.
        flush();path[#path+1]={kind="shift_lock",enabled=false}
        if not zoomed then camera("zoom_in",10) end
        pitch(4);zoom(5)
        path[#path+1]={kind="shift_lock",enabled=true}
    end
    if p.from=="hive" then
        assert(p.slot>=1 and p.slot<=6,"Confirmed hive slot required")
        -- Align against the hive wall, cross to the middle hive, then reach
        -- the spawn platform with the shared hive heading still unchanged.
        walk(5,F)
        if p.slot~=3 then walk(9.2*math.abs(p.slot-3),p.slot>3 and R or L) end
        walk(21,B);flush()
        scout_camera()
    elseif p.from=="spawn-start" then
        -- Fresh joins already stand at the circuit's start. The shared spawn
        -- direction check supplies yaw; no hive claim or return walk is needed.
        scout_camera(true)
    elseif p.from=="spawn" then
        turn("rotate_right");held={F};hold(10);glide(700);glide(700);move(1.25)
        turn("rotate_right");move(2.25);held={}
        jump(150);walk(2,F);walk(1.5,F,L);walk(4.5,F);walk(1.5,F,R)
        held={F};move(1.5);glide(650);walk(3,F)
        turn("rotate_left");jump(100);walk(4.5,F);walk(3.75,F,R)
        turn("rotate_left");jump(100);walk(1.5,F);hold(200)
        held={F};glide(650);move(2.5);jump(600);jump(400);turn("rotate_right");hold(200)
        glide(650);walk(.5,F);held={F};jump(100);move(2);turn("rotate_right");move(1)
        glide(650);move(2);turn("rotate_left",2);walk(3.25,F,R);walk(2.75,F)
        jump(100);walk(2,F);walk(2,R);pitch(6);zoom(10)
    elseif p.from=="pepper" then
        turn("rotate_left",3);jump(200);walk(1.5,B,L);hold(200);glide(2875)
        turn("rotate_right");hold(550);tap_jump(700);flush()
        path[#path+1]={kind="marker",name="cannon"}
        hold(1800);glide(1000);tap_jump(1);turn("rotate_right")
        walk(5,B);walk(.5,F);hold(100);jump(250);walk(7,B);walk(5,B,L)
        walk(2,R,F);walk(1,B,R);pitch(6,true);zoom(11)
    elseif p.from=="mountaintop" then
        pitch(6);walk(6,F,R);glide(700);turn("rotate_right");hold(2000)
        turn("rotate_right");hold(1500);turn("rotate_right");hold(550);tap_jump(750)
        walk(3,F,L);walk(7,F);walk(3,B,R);walk(6,R);zoom(10)
    elseif p.from=="spider" then
        walk(2,L);walk(2,F,L);walk(2,F);turn("rotate_right");jump(150);walk(2,F)
        held={F};move(5);glide(750);glide(750);move(6);jump(150);move(7)
        walk(7,F,L);hold(200);turn("rotate_left");walk(2,B,R);walk(4,B)
        jump(200);walk(1,B);hold(450);pitch(6);zoom(10)
    elseif p.from=="cactus" then
        turn("rotate_left");glide(1000);walk(14,F);walk(5,F,L);jump(150)
        walk(12,F);walk(4,F,L);turn("rotate_left");hold(200);walk(8,B)
        -- Bound the original's keys held during/after camera adjustment.
        held={B,R};hold(500);held={};pitch(5);zoom(11)
    else error("Unknown Vicious scout segment") end
    held={};hold(1);flush()
    return path
end
