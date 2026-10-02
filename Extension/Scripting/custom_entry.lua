-- Embedded by the SDK. C++ supplies id, source and settings as escaped values.
local engine = _G
local native_error, native_tostring = error, tostring
local native_require, native_print = require, print
local console_print = ingamePrint
local native_debug = debug
local native_load, native_loadstring, native_setfenv = load, loadstring, setfenv
local virtual_path = "Scripts/Custom/" .. id
local directory = virtual_path:match("^(.*)/") .. "/"
local mod = { api_version = 1, id = id, file = virtual_path, directory = directory, settings = settings }
local environment = setmetatable({ mod = mod }, { __index = engine })
environment._G = environment

local registry = engine.ReSkateCustom
if type(registry) ~= "table" then
    registry = { api_version = 1, loaded = {}, failed = {} }
    engine.ReSkateCustom = registry
end

function mod.log(message, level)
    level = string.upper(level or "INFO")
    if level ~= "DEBUG" and level ~= "INFO" and level ~= "WARN" and level ~= "ERROR" and level ~= "FATAL" then
        native_error("Unknown log level: " .. level, 2)
    end
    local text = "[Custom/" .. id .. "] " .. native_tostring(message)
    if console_print then console_print(level, text) else native_print(text) end
end

local function compile(text, name)
    if native_setfenv and native_loadstring then
        local chunk, err = native_loadstring(text, "@" .. name)
        if chunk then native_setfenv(chunk, environment) end
        return chunk, err
    end
    return native_load(text, "@" .. name, "t", environment)
end

-- Helpers execute in the same mod environment and are not automatically scanned.
function mod.import(relative)
    if type(relative) ~= "string" or relative == "" or relative:sub(1, 1) == "/"
        or relative:find("\\", 1, true) or relative:find(":", 1, true)
        or relative:find("%z") or relative:find("//", 1, true) then
        native_error("mod.import requires a relative Lua filename", 2)
    end
    for segment in relative:gmatch("[^/]+") do
        if segment == "." or segment == ".." then native_error("Invalid helper path", 2) end
    end
    if relative:sub(-4):lower() ~= ".lua" then native_error("Helper files must end in .lua", 2) end
    local name = directory .. relative
    local vfs = engine.vfs or native_require("vfs")
    local file = vfs.open(name, "r")
    if not file then native_error("Cannot open helper: " .. name, 2) end
    local ok, text = pcall(function() return file:read("*all") end)
    file:close()
    if not ok then native_error(text, 2) end
    local chunk, err = compile(text, name)
    if not chunk then native_error(err, 2) end
    return chunk()
end

-- These are startup settings. The engine reapplies them after all scripts run.
function mod.apply_settings(text)
    native_require("Frost.Core").parseKeyValueCfgString("Custom/" .. id, text, engine)
end

local function traceback(err)
    local message = native_tostring(err)
    if native_debug and native_debug.traceback then return native_debug.traceback(message, 2) end
    return message
end
local ok, result = xpcall(function()
    local chunk, err = compile(source, virtual_path)
    if not chunk then native_error(err, 0) end
    return chunk()
end, traceback)
if not ok then
    registry.failed[id] = result
    mod.log(result, "ERROR")
    native_error("Custom/" .. id .. ": " .. result, 0)
end
registry.loaded[id] = result == nil and true or result
registry.failed[id] = nil
