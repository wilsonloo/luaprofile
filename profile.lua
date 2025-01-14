local c = require "profile.c"

local M = {
}

-- luacheck: ignore coroutine on_coroutine_destory
local old_co_create = coroutine.create
local old_co_wrap = coroutine.wrap


local exists = 0
function M.start()
    if exists == 0 then
        c.start()
    end
    exists = exists + 1
end

function M.stop()
    local record_time, nodes = c.dump()
    exists = exists - 1
    if exists <= 0  then
        exists = 0
        c.stop()
    end
    return {time = record_time, nodes = nodes}
end


return M
