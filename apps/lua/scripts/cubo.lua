-- CUBO - the bench
--
-- A wireframe cube with the maths done per vertex in Lua: two rotations, a
-- perspective divide and twelve lines, every frame. That is the thing worth
-- measuring, because it is what tells you whether a game can be written in
-- Lua or whether the moving parts have to go into C.
--
-- Tap to add another cube. The counter at the bottom says how many frames a
-- second and how many vertices went through the maths in that second, so the
-- number keeps meaning something as the load grows.

local CX, CY = aos.W // 2, aos.H // 2
local DIST, ZOOM = 3.4, 46

local verts = {
    {-1,-1,-1}, { 1,-1,-1}, { 1, 1,-1}, {-1, 1,-1},
    {-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1},
}
local edges = {
    {1,2},{2,3},{3,4},{4,1},
    {5,6},{6,7},{7,8},{8,5},
    {1,5},{2,6},{3,7},{4,8},
}
local colours = { 0x00E5FF, 0xFF4081, 0xFFD740, 0x69F0AE, 0xB388FF, 0xFF8A65 }

local cubes = 1
local ang   = 0
local sx, sy = {}, {}          -- projected points, reused: no garbage per frame

local frames, verts_done = 0, 0
local fps, vps = 0, 0
local window = 0

function tick(dt)
    ang = ang + dt * 0.0015
    window = window + dt
    if window >= 1000 then
        fps, vps = frames, verts_done
        frames, verts_done, window = 0, 0, 0
    end
end

local function project(v, a, b, ox)
    local ca, sa = math.cos(a), math.sin(a)
    local cb, sb = math.cos(b), math.sin(b)

    local x, y, z = v[1], v[2], v[3]
    x, z = x * ca - z * sa, x * sa + z * ca      -- around Y
    y, z = y * cb - z * sb, y * sb + z * cb      -- around X

    local p = ZOOM / (DIST + z)
    return CX + x * p + ox, CY + y * p
end

function draw()
    aos.clear(0x05050C)

    for c = 1, cubes do
        local a = ang + c * 0.7
        local b = ang * 0.6 + c * 0.4
        local ox = (c - 1) % 3 * 0 + math.sin(ang + c) * (cubes > 1 and 40 or 0)
        local col = colours[(c - 1) % #colours + 1]

        for i = 1, 8 do
            sx[i], sy[i] = project(verts[i], a, b, ox)
        end
        for i = 1, 12 do
            local e = edges[i]
            aos.line(sx[e[1]] // 1, sy[e[1]] // 1,
                     sx[e[2]] // 1, sy[e[2]] // 1, col)
        end
        verts_done = verts_done + 8
    end
    frames = frames + 1

    aos.rect(0, aos.H - 24, aos.W, 24, 0x000000)
    local script_ms, screen_ms, frame_ms = aos.stats()
    aos.text(4, aos.H - 20, string.format("%d CUBOS  %d FPS  %d VERT/S",
             cubes, fps, vps), 0x9FA8DA, 1)
    aos.text(4, aos.H - 10, string.format("GUION %d MS  PANTALLA %d MS  CUADRO %d MS",
             script_ms, screen_ms, frame_ms), 0x6F7A99, 1)
end

-- "down" and not "move": LVGL repeats the press for as long as the finger
-- stays there, so acting on anything but the first event adds a cube per
-- frame, which is how this was found.
function touch(x, y, ev)
    if ev == "down" then
        cubes = cubes + 1
        if cubes > 24 then cubes = 1 end
        aos.beep(900 + cubes * 40, 20)
    end
end
