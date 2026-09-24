-- @name Gestos
--
-- The two ways a script gets more than one finger (v0.6.0):
--
--   gesture(ev, x, y, a, b, c)  what the fingers MEAN, already filtered:
--                               a pinch zooms this little star field about
--                               the point between the fingers, one finger
--                               drags it, a double tap brings it home.
--   aos.fingers()               where each finger IS, with an id that stays
--                               while it stays down: the rings.
--
-- The world is drawn again every frame (aos.clear), so it costs all the rows;
-- for a zoomable world that is the honest price.

local BG   = 0x05050C
local zoom, ox, oy = 1.0, 0, 0     -- screen = world * zoom + (ox, oy)
local stars = {}
local last = ""

function init()
    for i = 1, 90 do
        stars[i] = { x = math.random(-200, 380), y = math.random(-200, 420),
                     c = ({ 0xFFFFFF, 0xFFD740, 0x69F0AE, 0x00E5FF })[math.random(1, 4)] }
    end
    ox, oy = aos.W / 2 - 90, aos.H / 2 - 110
end

function gesture(ev, x, y, a, b, c)
    if ev == "pinch" then
        local nz = zoom * a
        if nz < 0.4 then nz = 0.4 end
        if nz > 6 then nz = 6 end
        local k = nz / zoom
        ox = x - (x - ox) * k + b          -- the point under the fingers stays
        oy = y - (y - oy) * k + c
        zoom = nz
    elseif ev == "drag" then
        ox, oy = ox + a, oy + b
    elseif ev == "double" then
        zoom, ox, oy = 1.0, aos.W / 2 - 90, aos.H / 2 - 110
    end
    if ev ~= "pinch" and ev ~= "drag" then last = string.upper(ev) end
end

function draw()
    aos.clear(BG)
    for i = 1, #stars do
        local s = stars[i]
        local sx, sy = math.floor(s.x * zoom + ox), math.floor(s.y * zoom + oy)
        if sx > -4 and sx < aos.W + 4 and sy > 18 and sy < aos.H + 4 then
            local r = math.floor(zoom * 1.5)
            if r < 1 then aos.pixel(sx, sy, s.c) else aos.disc(sx, sy, r, s.c) end
        end
    end
    local n, id1, x1, y1, id2, x2, y2 = aos.fingers()
    if n >= 1 then aos.ring(x1, y1, 12, 0x2EC4FF) end
    if n >= 2 then aos.ring(x2, y2, 12, 0xFF3B6B) end
    aos.rect(0, 0, aos.W, 16, 0x101020)
    aos.text(4, 5, string.upper(string.format("x%.1f %s", zoom, last)), 0x8090B0, 1)
end
