return function()
    local actions={}
    for _,turn in ipairs({{"zoom_in",10},{"pitch_up",10},{"pitch_down",4},{"rotate_right",8},{"pitch_down",1},{"rotate_right",8}}) do
        for i=1,turn[2] do actions[#actions+1]={keys={turn[1]},ms=50,label="Looking for hive reference"} end
    end
    return {{kind="checkpoint",detector="hive.backboard",timeout=45000,age=2000,attempts=0,approach=actions}}
end
