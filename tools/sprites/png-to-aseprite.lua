-- png-to-aseprite.lua — build an editable .aseprite from a folder of PNG frames (COM-296).
--
-- Turns raw frames (e.g. from the PixelLab MCP) into a normal Aseprite file with tags,
-- per-frame durations and the project's locked palette, ready for hand touch-ups and for
-- `export-sprites.sh`. Local-only: needs Aseprite, which never runs in CI.
--
-- Usage (from the repo root):
--   aseprite -b \
--     --script-param manifest=apps/tamagotchi-plus/assets/src/pet/manifest.json \
--     --script-param palette=apps/tamagotchi-plus/assets/palette.hex \
--     --script-param out=apps/tamagotchi-plus/assets/pet.aseprite \
--     --script tools/sprites/png-to-aseprite.lua
--
-- An existing `out` file is never overwritten unless you also pass --script-param force=true,
-- since re-importing throws away any hand touch-ups made in that .aseprite.
--
-- manifest.json (frame paths are relative to the manifest's folder; tags play in order):
--   {
--     "tags": [
--       { "name": "idle",  "direction": "forward",
--         "frames": [ { "file": "idle/00.png", "ms": 400 }, { "file": "idle/01.png", "ms": 200 } ] },
--       { "name": "happy", "direction": "forward", "frames": [ ... ] }
--     ]
--   }
-- direction is optional: forward (default), reverse or pingpong. Every frame must be the
-- same size.
--
-- The sprite is converted to Indexed mode on the locked palette (15 colours from
-- palette.hex + index 0 = transparent), so every pixel lands on a palette colour by
-- nearest match. That's what cleans up stray AI colours before the build's strict palette
-- check. Pixels with alpha < 128 become transparent and everything else becomes opaque,
-- since the device has no alpha blending.

-- Aseprite's Lua has no os.exit; a raised error aborts the batch run with a non-zero exit code.
local function fail(msg)
  error("png-to-aseprite: " .. msg, 0)
end

local params = app.params
for _, key in ipairs({ "manifest", "palette", "out" }) do
  if not params[key] or params[key] == "" then fail("missing --script-param " .. key .. "=...") end
end

local function readFile(path)
  local f = io.open(path, "rb")
  if not f then fail("can't read " .. path) end
  local data = f:read("a")
  f:close()
  return data
end

-- ── Palette ─────────────────────────────────────────────────────────────────────────
local colours = {}
for line in readFile(params.palette):gmatch("[^\r\n]+") do
  local hex = line:gsub(";.*", ""):gsub("%s", ""):gsub("^#", "")
  if hex ~= "" then
    if not hex:match("^%x%x%x%x%x%x$") then fail(params.palette .. ": bad colour '" .. line .. "'") end
    colours[#colours + 1] = Color{ r = tonumber(hex:sub(1, 2), 16), g = tonumber(hex:sub(3, 4), 16),
                                   b = tonumber(hex:sub(5, 6), 16), a = 255 }
  end
end
if #colours ~= 15 then fail(params.palette .. ": expected 15 colours, found " .. #colours) end

local palette = Palette(16)
palette:setColor(0, Color{ r = 0, g = 0, b = 0, a = 0 })
for i, c in ipairs(colours) do palette:setColor(i, c) end

-- ── Manifest ────────────────────────────────────────────────────────────────────────
if app.fs.isFile(params.out) and params.force ~= "true" then
  fail(params.out .. " already exists; pass --script-param force=true to regenerate it "
       .. "(this discards any hand edits made in it)")
end

-- Aseprite's json.decode returns userdata (not Lua tables) and raises on malformed input.
local okJson, manifest = pcall(json.decode, readFile(params.manifest))
if not okJson or manifest == nil then fail(params.manifest .. ": invalid JSON (" .. tostring(manifest) .. ")") end
local baseDir = app.fs.filePath(params.manifest)
if not manifest.tags or #manifest.tags == 0 then fail(params.manifest .. ": no tags") end

local frames = {}  -- { image, ms, tagIndex }
for t, tag in ipairs(manifest.tags) do
  if not tag.name or not tag.frames or #tag.frames == 0 then fail("tag #" .. t .. " needs a name and frames") end
  for _, f in ipairs(tag.frames) do
    local path = app.fs.joinPath(baseDir, f.file)
    if not app.fs.isFile(path) then fail("missing frame " .. path) end
    local okImg, img = pcall(function() return Image{ fromFile = path } end)
    if not okImg or not img then fail("can't decode frame " .. path) end
    if img.colorMode ~= ColorMode.RGB then
      -- Normalise indexed/greyscale PNGs to RGBA so the alpha threshold below applies uniformly.
      local rgb = Image(img.width, img.height, ColorMode.RGB)
      rgb:drawImage(img)
      img = rgb
    end
    frames[#frames + 1] = { image = img, ms = f.ms or 100, tag = t }
  end
end

local w, h = frames[1].image.width, frames[1].image.height
for i, f in ipairs(frames) do
  if f.image.width ~= w or f.image.height ~= h then
    fail(string.format("frame %d is %dx%d, expected %dx%d", i, f.image.width, f.image.height, w, h))
  end
end

-- ── Build the sprite ────────────────────────────────────────────────────────────────
local sprite = Sprite(w, h, ColorMode.RGB)
app.activeSprite = sprite
sprite:setPalette(palette)
local layer = sprite.layers[1]
layer.name = "pet"

local pc = app.pixelColor
for i, f in ipairs(frames) do
  local frame = i == 1 and sprite.frames[1] or sprite:newEmptyFrame(i)
  frame.duration = f.ms / 1000
  -- Threshold alpha: fully opaque or fully transparent, nothing in between.
  local img = f.image:clone()
  for it in img:pixels() do
    local v = it()
    if pc.rgbaA(v) < 128 then
      it(pc.rgba(0, 0, 0, 0))
    else
      it(pc.rgba(pc.rgbaR(v), pc.rgbaG(v), pc.rgbaB(v), 255))
    end
  end
  sprite:newCel(layer, frame, img, Point(0, 0))
end

local directions = { forward = AniDir.FORWARD, reverse = AniDir.REVERSE, pingpong = AniDir.PING_PONG }
local first = 1
for t, tag in ipairs(manifest.tags) do
  local count = #tag.frames
  local newTag = sprite:newTag(first, first + count - 1)
  newTag.name = tag.name
  newTag.aniDir = directions[tag.direction or "forward"] or fail("tag " .. tag.name .. ": bad direction")
  first = first + count
end

-- Map every pixel onto the locked palette (nearest colour; index 0 stays reserved for transparent).
app.command.ChangePixelFormat{ format = "indexed", dithering = "none" }
sprite.transparentColor = 0

sprite:saveAs(params.out)
print(string.format("png-to-aseprite: %s: %dx%d, %d frames, %d tags", params.out, w, h, #frames, #manifest.tags))
