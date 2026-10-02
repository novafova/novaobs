--[[
Nova OBS - "Clip Saved" notifications for the OBS replay buffer.

Add this file in OBS under Tools > Scripts (Lua is built into OBS; nothing to
install). When the replay buffer is saved, a small card slides in at the top
right of the screen showing "Clip Saved" with a thumbnail of the clip.

NovaOverlay.exe must sit in the same folder as this script.

This never touches the game process (no injection or hooks), so it is safe
with anti-cheat. The card is an ordinary always-on-top, click-through window.
]]

obs = obslua
local ffi = require("ffi")

local CP_UTF8 = 65001
local CREATE_NO_WINDOW = 0x08000000
local BELOW_NORMAL_PRIORITY_CLASS = 0x00004000
local INVALID_FILE_ATTRIBUTES = 0xFFFFFFFF

local cdef_ok = pcall(ffi.cdef, [[
typedef struct {
    uint32_t cb;
    void* lpReserved; void* lpDesktop; void* lpTitle;
    uint32_t dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    uint16_t wShowWindow, cbReserved2;
    void* lpReserved2; void* hStdInput; void* hStdOutput; void* hStdError;
} NOVA_STARTUPINFOW;
typedef struct {
    void* hProcess; void* hThread; uint32_t dwProcessId; uint32_t dwThreadId;
} NOVA_PROCESS_INFORMATION;
int MultiByteToWideChar(unsigned int CodePage, uint32_t dwFlags, const char* lpMultiByteStr,
                        int cbMultiByte, uint16_t* lpWideCharStr, int cchWideChar);
int CreateProcessW(const uint16_t* lpApplicationName, uint16_t* lpCommandLine, void* lpProcessAttributes,
                   void* lpThreadAttributes, int bInheritHandles, uint32_t dwCreationFlags, void* lpEnvironment,
                   const uint16_t* lpCurrentDirectory, NOVA_STARTUPINFOW* lpStartupInfo,
                   NOVA_PROCESS_INFORMATION* lpProcessInformation);
int CloseHandle(void* hObject);
uint32_t GetLastError(void);
uint32_t GetFileAttributesW(const uint16_t* lpFileName);
int DeleteFileW(const uint16_t* lpFileName);
]])

local settings = {
    enabled = true,
    position = "top-right",
    duration = 4.0,
    scale = 100,
    show_thumbnail = false,
    play_sound = false,
    hide_from_capture = true,
}

local dir = ""
local pending = nil            -- { clip = string|nil, test = bool } waiting for its screenshot
local late_shots = 0           -- our screenshots that timed out; delete them if they still arrive

local SCREENSHOT_EVENT = obs.OBS_FRONTEND_EVENT_SCREENSHOT_TAKEN
local CAN_SCREENSHOT = SCREENSHOT_EVENT ~= nil
    and obs.obs_frontend_take_screenshot ~= nil
    and obs.obs_frontend_get_last_screenshot ~= nil

local function log(level, msg)
    obs.script_log(level, "[Nova OBS] " .. msg)
end

-- ---------------------------------------------------------------------------
-- Win32 helpers (UTF-8 aware, so non-English folder names work)
-- ---------------------------------------------------------------------------

local function wide(s)
    local n = ffi.C.MultiByteToWideChar(CP_UTF8, 0, s, -1, nil, 0)
    if n <= 0 then return nil end
    local buf = ffi.new("uint16_t[?]", n)
    ffi.C.MultiByteToWideChar(CP_UTF8, 0, s, -1, buf, n)
    return buf
end

local function file_exists(path)
    local w = path and path ~= "" and wide(path)
    return w ~= nil and w ~= false and ffi.C.GetFileAttributesW(w) ~= INVALID_FILE_ATTRIBUTES
end

local function delete_file(path)
    local w = path and path ~= "" and wide(path)
    if w then ffi.C.DeleteFileW(w) end
end

-- Quote one argument following the CommandLineToArgvW rules.
local function quote(a)
    a = tostring(a)
    if a == "" then return '""' end
    if not a:find('[%s"]') then return a end
    local out, bs = { '"' }, 0
    for i = 1, #a do
        local c = a:sub(i, i)
        if c == "\\" then
            bs = bs + 1
        elseif c == '"' then
            out[#out + 1] = string.rep("\\", bs * 2 + 1) .. '"'
            bs = 0
        else
            out[#out + 1] = string.rep("\\", bs) .. c
            bs = 0
        end
    end
    out[#out + 1] = string.rep("\\", bs * 2) .. '"'
    return table.concat(out)
end

local function overlay_exe()
    return dir .. "NovaOverlay.exe"
end

-- ---------------------------------------------------------------------------
-- Notification flow
-- ---------------------------------------------------------------------------

local function launch_overlay(clip, shot, is_test)
    local exe = overlay_exe()
    if not cdef_ok then
        log(obs.LOG_WARNING, "LuaJIT FFI unavailable; cannot start the overlay.")
        delete_file(shot)
        return
    end
    if not file_exists(exe) then
        log(obs.LOG_WARNING, "NovaOverlay.exe not found next to the script: " .. exe)
        delete_file(shot)
        return
    end

    local args = {
        quote(exe),
        "--duration", string.format("%.2f", settings.duration),
        "--position", settings.position,
        "--scale", tostring(settings.scale),
    }
    if clip and clip ~= "" then
        args[#args + 1] = "--clip"
        args[#args + 1] = quote(clip)
    end
    if shot and shot ~= "" then
        args[#args + 1] = "--screenshot"
        args[#args + 1] = quote(shot)
        args[#args + 1] = "--delete-screenshot"
    end
    if not settings.show_thumbnail then args[#args + 1] = "--no-thumbnail" end
    if settings.play_sound then args[#args + 1] = "--sound" end
    if not settings.hide_from_capture then args[#args + 1] = "--no-hide-from-capture" end
    if is_test then args[#args + 1] = "--test" end

    local wexe = wide(exe)
    local wcmd = wide(table.concat(args, " "))
    local wdir = wide(dir)
    local si = ffi.new("NOVA_STARTUPINFOW")
    si.cb = ffi.sizeof("NOVA_STARTUPINFOW")
    local pi = ffi.new("NOVA_PROCESS_INFORMATION")

    local ok = ffi.C.CreateProcessW(wexe, wcmd, nil, nil, 0,
        CREATE_NO_WINDOW + BELOW_NORMAL_PRIORITY_CLASS, nil, wdir, si, pi)
    if ok ~= 0 then
        ffi.C.CloseHandle(pi.hThread)
        ffi.C.CloseHandle(pi.hProcess)
    else
        log(obs.LOG_WARNING, "Failed to start NovaOverlay.exe (error " .. tostring(ffi.C.GetLastError()) .. ")")
        delete_file(shot)
    end
end

local function screenshot_timeout()
    -- OBS never reported the screenshot; show the card without a thumbnail.
    obs.timer_remove(screenshot_timeout)
    if pending ~= nil then
        local p = pending
        pending = nil
        late_shots = late_shots + 1
        launch_overlay(p.clip, nil, p.test)
    end
end

local function notify(clip, is_test)
    if settings.show_thumbnail and CAN_SCREENSHOT then
        -- OBS's own screenshot of the program output. It is taken on OBS's
        -- render thread and saved on a background thread, and works even when
        -- the game is fullscreen.
        pending = { clip = clip, test = is_test }
        obs.timer_remove(screenshot_timeout)
        obs.timer_add(screenshot_timeout, 2500)
        obs.obs_frontend_take_screenshot()
    else
        launch_overlay(clip, nil, is_test)
    end
end

local function on_screenshot_taken()
    if pending == nil then
        if late_shots > 0 then          -- ours, but it arrived after the timeout
            late_shots = late_shots - 1
            delete_file(obs.obs_frontend_get_last_screenshot())
        end
        return                          -- otherwise a screenshot the user took themselves
    end
    obs.timer_remove(screenshot_timeout)
    local p = pending
    pending = nil
    local shot = obs.obs_frontend_get_last_screenshot()
    if shot ~= nil and not file_exists(shot) then shot = nil end
    launch_overlay(p.clip, shot, p.test)
end

local function get_last_replay()
    if obs.obs_frontend_get_last_replay ~= nil then
        local path = obs.obs_frontend_get_last_replay()
        if path ~= nil and path ~= "" then return path end
    end
    local rb = obs.obs_frontend_get_replay_buffer_output()
    if rb == nil then return nil end
    local cd = obs.calldata_create()
    local ph = obs.obs_output_get_proc_handler(rb)
    obs.proc_handler_call(ph, "get_last_replay", cd)
    local path = obs.calldata_string(cd, "path")
    obs.calldata_destroy(cd)
    obs.obs_output_release(rb)
    return path
end

local function on_event(event)
    if event == obs.OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED then
        if settings.enabled then notify(get_last_replay(), false) end
    elseif SCREENSHOT_EVENT ~= nil and event == SCREENSHOT_EVENT then
        on_screenshot_taken()
    end
end

local function on_test_clicked(props, prop)
    notify(nil, true)
    return false
end

-- ---------------------------------------------------------------------------
-- OBS script API
-- ---------------------------------------------------------------------------

function script_description()
    return [[<h3 style="color:#9d82d6">Nova OBS &mdash; Clip Saved Notifications</h3>
<p>Shows a <b>Clip Saved</b> card with a thumbnail when the replay buffer is saved.</p>
<p>Keep <b>NovaOverlay.exe</b> in the same folder as this script. No other installs needed.</p>]]
end

function script_properties()
    local p = obs.obs_properties_create()
    obs.obs_properties_add_bool(p, "enabled", "Show \"Clip Saved\" notification")

    local pos = obs.obs_properties_add_list(p, "position", "Position",
        obs.OBS_COMBO_TYPE_LIST, obs.OBS_COMBO_FORMAT_STRING)
    obs.obs_property_list_add_string(pos, "Top right", "top-right")
    obs.obs_property_list_add_string(pos, "Top left", "top-left")
    obs.obs_property_list_add_string(pos, "Bottom right", "bottom-right")
    obs.obs_property_list_add_string(pos, "Bottom left", "bottom-left")

    obs.obs_properties_add_float_slider(p, "duration", "Display time (seconds)", 1.0, 15.0, 0.5)
    obs.obs_properties_add_int_slider(p, "scale", "Size (%)", 50, 200, 5)
    obs.obs_properties_add_bool(p, "show_thumbnail", "Show clip thumbnail")
    obs.obs_properties_add_bool(p, "play_sound", "Play sound")
    obs.obs_properties_add_bool(p, "hide_from_capture", "Hide notification from recordings/stream")
    obs.obs_properties_add_button(p, "test", "Show test notification", on_test_clicked)
    return p
end

function script_defaults(s)
    obs.obs_data_set_default_bool(s, "enabled", true)
    obs.obs_data_set_default_string(s, "position", "top-right")
    obs.obs_data_set_default_double(s, "duration", 4.0)
    obs.obs_data_set_default_int(s, "scale", 100)
    obs.obs_data_set_default_bool(s, "show_thumbnail", false)
    obs.obs_data_set_default_bool(s, "play_sound", false)
    obs.obs_data_set_default_bool(s, "hide_from_capture", true)
end

function script_update(s)
    settings.enabled = obs.obs_data_get_bool(s, "enabled")
    local pos = obs.obs_data_get_string(s, "position")
    settings.position = (pos ~= nil and pos ~= "") and pos or "top-right"
    settings.duration = obs.obs_data_get_double(s, "duration")
    settings.scale = obs.obs_data_get_int(s, "scale")
    settings.show_thumbnail = obs.obs_data_get_bool(s, "show_thumbnail")
    settings.play_sound = obs.obs_data_get_bool(s, "play_sound")
    settings.hide_from_capture = obs.obs_data_get_bool(s, "hide_from_capture")
end

function script_load(s)
    dir = script_path() or ""
    if dir ~= "" and not dir:match("[/\\]$") then dir = dir .. "/" end
    obs.obs_frontend_add_event_callback(on_event)
    if not CAN_SCREENSHOT then
        log(obs.LOG_INFO, "This OBS version can't report screenshots; the card will use a placeholder thumbnail.")
    end
end

function script_unload()
    obs.timer_remove(screenshot_timeout)
end
