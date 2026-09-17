-- @name Pelotas
--
-- The other way of drawing: nothing is cleared. The background is painted
-- once in init() and after that each ball erases its own old position and
-- draws the new one, so the only rows that change are the ones the balls are
-- on. The app notices by itself -every primitive marks the box it touches-
-- and pushes only those rows at the panel.
--
-- cubo.lua is the opposite and both are right: it calls aos.clear() every
-- frame, so every row is dirty and the frame costs what it always did. The
-- counter in the top right says which of the two is happening: the "n/224" is
-- how many rows went out.
--
-- The limit of erasing this way is that the background has to be a flat
-- colour underneath, which is why the title lives above the field and the
-- balls never reach it.

local BG    = 0x05050C
local TOP   = 26                -- the field starts below the title
local N     = 4
local balls = {}

local COLOURS = { 0x00E5FF, 0xFF4081, 0xFFD740, 0x69F0AE }

function init()
    aos.clear(BG)
    aos.text(6, 8, "PELOTAS", 0x3E4A63, 1)
    aos.rect(0, TOP - 3, aos.W, 1, 0x1B2330)

    for i = 1, N do
        balls[i] = {
            x  = 20 + i * 30,
            y  = TOP + 20 + i * 14,
            dx = (i % 2 == 0) and 0.9 or -1.1,
            dy = 0.7 + i * 0.15,
            r  = 6 + i,
            c  = COLOURS[i],
        }
    end
end

function tick(dt)
    local step = dt / 16          -- so it moves the same on a slow frame
    for i = 1, N do
        local b = balls[i]

        -- erase where it was: a disc of the background over the old place,
        -- one pixel wider so no edge is left behind
        aos.disc(b.x // 1, b.y // 1, b.r + 1, BG)

        b.x = b.x + b.dx * step
        b.y = b.y + b.dy * step
        if b.x - b.r < 1 then b.x, b.dx = 1 + b.r, -b.dx end
        if b.x + b.r > aos.W - 2 then b.x, b.dx = aos.W - 2 - b.r, -b.dx end
        if b.y - b.r < TOP then b.y, b.dy = TOP + b.r, -b.dy end
        if b.y + b.r > aos.H - 2 then b.y, b.dy = aos.H - 2 - b.r, -b.dy end
    end
end

function draw()
    for i = 1, N do
        local b = balls[i]
        aos.disc(b.x // 1, b.y // 1, b.r, b.c)
    end
end

-- A tap scatters them, which is the cheapest way to see that the rows being
-- pushed follow the balls around.
function touch(x, y, ev)
    if ev ~= "down" then return end
    for i = 1, N do
        local b = balls[i]
        b.dx = (x < aos.W / 2) and -math.abs(b.dx) or math.abs(b.dx)
        b.dy = (y < aos.H / 2) and -math.abs(b.dy) or math.abs(b.dy)
    end
    aos.beep(700 + N * 60, 20)
end
