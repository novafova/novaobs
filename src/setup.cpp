// Nova OBS Setup - one-click installer for non-technical users.
//
//  * Installs per-user to %LOCALAPPDATA%\NovaOBS (no admin prompt).
//  * Adds the script to every OBS scene collection, so there is nothing to
//    configure by hand. Existing Nova OBS settings are carried over.
//  * Backs up each scene collection before changing it.
//  * Registers in Windows "Apps" so it can be uninstalled normally.
//
// Command line:
//   (none)                  interactive install / update / uninstall
//   --uninstall             interactive uninstall (used by Windows "Apps")
//   --test-root <dir>       silent install into <dir>\install using <dir>\scenes (testing)
//   --test-root <dir> --uninstall   silent uninstall of the above
//   --portable-root <dir>   silent install into a portable OBS package

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define IDR_SCRIPT 101
#define IDR_OVERLAY 102

static const wchar_t* kTitle = L"Nova OBS Setup";
static const wchar_t* kVersion = L"1.0.0";
static const wchar_t* kUninstallKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NovaOBS";
static const wchar_t* kScriptName = L"nova_clip_notify.lua";
static const wchar_t* kOverlayName = L"NovaOverlay.exe";
static const wchar_t* kSetupName = L"NovaOBS-Setup.exe";
static const char* kScriptNameA = "nova_clip_notify.lua";

static bool g_silent = false;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) out = p;
    CoTaskMemFree(p);
    return out;
}

static std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static bool FileExists(const std::wstring& p) {
    DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static bool ReadAll(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart < (256LL << 20);
    if (ok) {
        out.resize((size_t)sz.QuadPart);
        DWORD rd = 0;
        ok = out.empty() || (ReadFile(h, &out[0], (DWORD)out.size(), &rd, nullptr) && rd == out.size());
    }
    CloseHandle(h);
    return ok;
}

static bool WriteAll(const std::wstring& path, const void* data, size_t size) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0;
    bool ok = size == 0 || (WriteFile(h, data, (DWORD)size, &wr, nullptr) && wr == size);
    ok = FlushFileBuffers(h) && ok;
    CloseHandle(h);
    return ok;
}

// Writes via a temp file + atomic replace, so a crash can never leave a half-written file.
static bool WriteAtomic(const std::wstring& path, const std::string& data) {
    std::wstring tmp = path + L".novaobs-tmp";
    if (!WriteAll(tmp, data.data(), data.size())) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

static void CloseRunningToasts() {
    HWND h = nullptr;
    while ((h = FindWindowExW(nullptr, h, L"NovaOBSClipToast", nullptr)) != nullptr) PostMessageW(h, WM_CLOSE, 0, 0);
}

// Writes a file, retrying for a few seconds in case a notification is on screen
// (the overlay exe is briefly locked while it runs).
static bool WriteWithRetry(const std::wstring& path, const void* data, size_t size) {
    for (int i = 0; i < 50; ++i) {
        if (WriteAll(path, data, size)) return true;
        CloseRunningToasts();
        Sleep(100);
    }
    return false;
}

static bool DeleteWithRetry(const std::wstring& path) {
    for (int i = 0; i < 50; ++i) {
        if (DeleteFileW(path.c_str())) return true;
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return true;
        CloseRunningToasts();
        Sleep(100);
    }
    return false;
}

static bool IsObsRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{sizeof(pe)};
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"obs64.exe") == 0 || _wcsicmp(pe.szExeFile, L"obs32.exe") == 0 ||
            _wcsicmp(pe.szExeFile, L"obs.exe") == 0) {
            found = true;
            break;
        }
    }
    CloseHandle(snap);
    return found;
}

static std::wstring FindObsExe() {
    std::vector<std::wstring> roots;
    for (HKEY hive : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        wchar_t buf[MAX_PATH];
        DWORD size = sizeof(buf);
        if (RegGetValueW(hive, L"SOFTWARE\\OBS Studio", nullptr, RRF_RT_REG_SZ, nullptr, buf, &size) == ERROR_SUCCESS)
            roots.push_back(buf);
    }
    roots.push_back(KnownFolder(FOLDERID_ProgramFiles) + L"\\obs-studio");
    for (auto& r : roots) {
        std::wstring exe = r + L"\\bin\\64bit\\obs64.exe";
        if (FileExists(exe)) return exe;
    }
    return {};
}

// ---------------------------------------------------------------------------
// Dialogs
// ---------------------------------------------------------------------------

struct Button {
    int id;
    const wchar_t* text;
};

static int Ask(const wchar_t* heading, const wchar_t* body, std::vector<Button> buttons, TASKDIALOG_COMMON_BUTTON_FLAGS common,
               bool commandLinks, PCWSTR icon = MAKEINTRESOURCEW(1)) {
    if (g_silent) return buttons.empty() ? IDOK : buttons.front().id;
    std::vector<TASKDIALOG_BUTTON> tb;
    for (auto& b : buttons) tb.push_back({b.id, b.text});
    TASKDIALOGCONFIG c{sizeof(c)};
    c.hInstance = GetModuleHandleW(nullptr);
    c.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW | (commandLinks ? TDF_USE_COMMAND_LINKS : 0);
    c.pszWindowTitle = kTitle;
    c.pszMainIcon = icon;
    c.pszMainInstruction = heading;
    c.pszContent = body;
    c.pButtons = tb.empty() ? nullptr : tb.data();
    c.cButtons = (UINT)tb.size();
    c.dwCommonButtons = common;
    int pressed = IDCANCEL;
    if (FAILED(TaskDialogIndirect(&c, &pressed, nullptr, nullptr))) {
        MessageBoxW(nullptr, body, heading, MB_OK);
        return IDOK;
    }
    return pressed;
}

static void Info(const wchar_t* heading, const std::wstring& body, PCWSTR icon = MAKEINTRESOURCEW(1)) {
    Ask(heading, body.c_str(), {}, TDCBF_OK_BUTTON, false, icon);
}

// Keeps asking until OBS is closed. Returns false if the user gives up.
static bool WaitForObsClosed() {
    if (g_silent) return true;  // test mode only touches copies, so OBS may stay open
    while (IsObsRunning()) {
        int r = Ask(L"Please close OBS first",
                    L"OBS is open right now. Close it completely (check the system tray too), then click Try again.",
                    {}, TDCBF_RETRY_BUTTON | TDCBF_CANCEL_BUTTON, false, TD_WARNING_ICON);
        if (r != IDRETRY) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Minimal lossless JSON (numbers and string escapes are kept verbatim)
// ---------------------------------------------------------------------------

struct Json {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    std::string raw;  // literal token, or string contents still escaped
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;

    Json* get(const char* key) {
        for (auto& m : members)
            if (m.first == key) return &m.second;
        return nullptr;
    }
    static Json object() { Json j; j.type = Obj; return j; }
    static Json array() { Json j; j.type = Arr; return j; }
    static Json str(const std::string& escaped) { Json j; j.type = Str; j.raw = escaped; return j; }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& s) : s_(s) {}
    bool parse(Json& out) {
        if (s_.size() >= 3 && (unsigned char)s_[0] == 0xEF && (unsigned char)s_[1] == 0xBB && (unsigned char)s_[2] == 0xBF) i_ = 3;
        if (!value(out, 0)) return false;
        ws();
        return i_ == s_.size();
    }

private:
    const std::string& s_;
    size_t i_ = 0;

    void ws() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }
    bool str(std::string& out) {
        if (i_ >= s_.size() || s_[i_] != '"') return false;
        ++i_;
        size_t start = i_;
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (c == '\\') { i_ += 2; continue; }
            if (c == '"') { out.assign(s_, start, i_ - start); ++i_; return true; }
            ++i_;
        }
        return false;
    }
    bool value(Json& v, int depth) {
        if (depth > 512) return false;
        ws();
        if (i_ >= s_.size()) return false;
        char c = s_[i_];
        if (c == '{') {
            v.type = Json::Obj;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
            for (;;) {
                ws();
                std::string key;
                if (!str(key)) return false;
                ws();
                if (i_ >= s_.size() || s_[i_] != ':') return false;
                ++i_;
                Json child;
                if (!value(child, depth + 1)) return false;
                v.members.emplace_back(std::move(key), std::move(child));
                ws();
                if (i_ >= s_.size()) return false;
                if (s_[i_] == ',') { ++i_; continue; }
                if (s_[i_] == '}') { ++i_; return true; }
                return false;
            }
        }
        if (c == '[') {
            v.type = Json::Arr;
            ++i_;
            ws();
            if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
            for (;;) {
                Json child;
                if (!value(child, depth + 1)) return false;
                v.items.push_back(std::move(child));
                ws();
                if (i_ >= s_.size()) return false;
                if (s_[i_] == ',') { ++i_; continue; }
                if (s_[i_] == ']') { ++i_; return true; }
                return false;
            }
        }
        if (c == '"') { v.type = Json::Str; return str(v.raw); }
        for (const char* lit : {"true", "false", "null"}) {
            size_t n = strlen(lit);
            if (s_.compare(i_, n, lit) == 0) {
                v.type = lit[0] == 'n' ? Json::Null : Json::Bool;
                v.raw = lit;
                i_ += n;
                return true;
            }
        }
        size_t start = i_;
        while (i_ < s_.size() && strchr("+-0123456789.eE", s_[i_])) ++i_;
        if (i_ == start) return false;
        v.type = Json::Num;
        v.raw.assign(s_, start, i_ - start);
        return true;
    }
};

static void WriteJson(const Json& v, std::string& out, int indent) {
    auto pad = [&](int n) { out.append((size_t)n * 4, ' '); };
    switch (v.type) {
    case Json::Null:
    case Json::Bool:
    case Json::Num: out += v.raw; break;
    case Json::Str: out += '"'; out += v.raw; out += '"'; break;
    case Json::Arr:
        if (v.items.empty()) { out += "[]"; break; }
        out += "[\n";
        for (size_t i = 0; i < v.items.size(); ++i) {
            pad(indent + 1);
            WriteJson(v.items[i], out, indent + 1);
            out += i + 1 < v.items.size() ? ",\n" : "\n";
        }
        pad(indent);
        out += ']';
        break;
    case Json::Obj:
        if (v.members.empty()) { out += "{}"; break; }
        out += "{\n";
        for (size_t i = 0; i < v.members.size(); ++i) {
            pad(indent + 1);
            out += '"';
            out += v.members[i].first;
            out += "\": ";
            WriteJson(v.members[i].second, out, indent + 1);
            out += i + 1 < v.members.size() ? ",\n" : "\n";
        }
        pad(indent);
        out += '}';
        break;
    }
}

static std::string JsonEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += c; }
        else if ((unsigned char)c < 0x20) { char b[8]; sprintf_s(b, "\\u%04x", c); o += b; }
        else o += c;
    }
    return o;
}

// ---------------------------------------------------------------------------
// OBS scene collections
// ---------------------------------------------------------------------------

static std::vector<std::wstring> SceneCollections(const std::wstring& dir) {
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.json").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return files;
    do {
        std::wstring name = fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        if (name.size() < 5 || _wcsicmp(name.c_str() + name.size() - 5, L".json") != 0) continue;
        files.push_back(dir + L"\\" + name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return files;
}

enum class EditResult { Changed, Unchanged, Failed };

// Adds (scriptPath non-empty) or removes (empty) the Nova OBS script entry.
static EditResult EditCollection(const std::wstring& file, const std::string& scriptPath) {
    std::string text;
    if (!ReadAll(file, text)) return EditResult::Failed;
    Json root;
    if (!JsonParser(text).parse(root) || root.type != Json::Obj) return EditResult::Failed;

    Json* modules = root.get("modules");
    if (!modules) {
        if (scriptPath.empty()) return EditResult::Unchanged;
        root.members.emplace_back("modules", Json::object());
        modules = &root.members.back().second;
    }
    if (modules->type != Json::Obj) return EditResult::Failed;

    Json* scripts = modules->get("scripts-tool");
    if (!scripts) {
        if (scriptPath.empty()) return EditResult::Unchanged;
        modules->members.emplace_back("scripts-tool", Json::array());
        scripts = &modules->members.back().second;
    }
    if (scripts->type != Json::Arr) return EditResult::Failed;

    if (!scriptPath.empty()) {
        size_t novaCount = 0;
        bool exactPath = false;
        for (auto& entry : scripts->items) {
            Json* path = entry.type == Json::Obj ? entry.get("path") : nullptr;
            if (path && path->type == Json::Str && path->raw.find(kScriptNameA) != std::string::npos) {
                ++novaCount;
                exactPath = path->raw == JsonEscape(scriptPath);
            }
        }
        if (novaCount == 1 && exactPath) return EditResult::Unchanged;
    }

    // Remove every existing Nova OBS entry (any location), remembering its settings.
    Json settings = Json::object();
    bool haveSettings = false, removed = false;
    std::vector<Json> kept;
    for (auto& entry : scripts->items) {
        Json* path = entry.type == Json::Obj ? entry.get("path") : nullptr;
        if (path && path->type == Json::Str && path->raw.find(kScriptNameA) != std::string::npos) {
            Json* s = entry.get("settings");
            if (!haveSettings && s && s->type == Json::Obj) { settings = *s; haveSettings = true; }
            removed = true;
            continue;
        }
        kept.push_back(std::move(entry));
    }
    scripts->items = std::move(kept);

    if (!scriptPath.empty()) {
        Json entry = Json::object();
        entry.members.emplace_back("path", Json::str(JsonEscape(scriptPath)));
        entry.members.emplace_back("settings", settings);
        scripts->items.push_back(std::move(entry));
    } else if (!removed) {
        return EditResult::Unchanged;
    }

    std::string out;
    WriteJson(root, out, 0);
    out += '\n';

    Json check;  // never write something OBS couldn't read back
    if (!JsonParser(out).parse(check)) return EditResult::Failed;

    std::wstring backup = file + L".novaobs-backup";
    if (!FileExists(backup) && !CopyFileW(file.c_str(), backup.c_str(), TRUE)) return EditResult::Failed;
    return WriteAtomic(file, out) ? EditResult::Changed : EditResult::Failed;
}

// ---------------------------------------------------------------------------
// Install / uninstall
// ---------------------------------------------------------------------------

struct Paths {
    std::wstring installDir, scenesDir;
    bool registry = true;
};

static bool ExtractResource(int id, const std::wstring& dest, DWORD& bytes) {
    HMODULE self = GetModuleHandleW(nullptr);
    HRSRC r = FindResourceW(self, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!r) return false;
    HGLOBAL g = LoadResource(self, r);
    DWORD size = SizeofResource(self, r);
    const void* data = g ? LockResource(g) : nullptr;
    if (!data) return false;
    bytes += size;
    return WriteWithRetry(dest, data, size);
}

static void WriteUninstallEntry(const std::wstring& dir, DWORD totalBytes) {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kUninstallKey, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    auto setStr = [&](const wchar_t* name, const std::wstring& v) {
        RegSetValueExW(key, name, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    };
    auto setDword = [&](const wchar_t* name, DWORD v) { RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE*)&v, sizeof(v)); };
    std::wstring setup = dir + L"\\" + kSetupName;
    setStr(L"DisplayName", L"Nova OBS (Clip Saved notifications)");
    setStr(L"DisplayVersion", kVersion);
    setStr(L"Publisher", L"novafova");
    setStr(L"URLInfoAbout", L"https://github.com/novafova/novaobs");
    setStr(L"InstallLocation", dir);
    setStr(L"DisplayIcon", setup + L",0");
    setStr(L"UninstallString", L"\"" + setup + L"\" --uninstall");
    setDword(L"NoModify", 1);
    setDword(L"NoRepair", 1);
    setDword(L"EstimatedSize", totalBytes / 1024 + 1);
    RegCloseKey(key);
}

static int Install(const Paths& p, bool update) {
    if (!WaitForObsClosed()) return 1;

    SHCreateDirectoryExW(nullptr, p.installDir.c_str(), nullptr);
    DWORD bytes = 0;
    std::wstring script = p.installDir + L"\\" + kScriptName;
    if (!ExtractResource(IDR_SCRIPT, script, bytes) ||
        !ExtractResource(IDR_OVERLAY, p.installDir + L"\\" + kOverlayName, bytes)) {
        Info(L"Installation failed", L"Couldn't copy the files to:\n" + p.installDir, TD_ERROR_ICON);
        return 1;
    }

    // Keep a copy of this installer next to the files so Windows can uninstall it.
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring setupCopy = p.installDir + L"\\" + kSetupName;
    if (_wcsicmp(self, setupCopy.c_str()) != 0) CopyFileW(self, setupCopy.c_str(), FALSE);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(setupCopy.c_str(), GetFileExInfoStandard, &fa)) bytes += fa.nFileSizeLow;
    if (p.registry) WriteUninstallEntry(p.installDir, bytes);

    // OBS stores script paths with forward slashes.
    std::string scriptUtf8 = ToUtf8(script);
    for (auto& ch : scriptUtf8)
        if (ch == '\\') ch = '/';

    auto collections = SceneCollections(p.scenesDir);
    int changed = 0, failed = 0;
    for (auto& f : collections) {
        EditResult r = EditCollection(f, scriptUtf8);
        if (r == EditResult::Changed || r == EditResult::Unchanged) ++changed;
        else ++failed;
    }

    if (collections.empty()) {
        Info(L"Almost done!",
             L"Nova OBS was installed, but OBS hasn't been set up on this PC yet.\n\n"
             L"Open OBS once, close it again, then run this installer one more time.");
        return 0;
    }
    if (failed > 0 && changed == 0) {
        Info(L"Couldn't turn on Nova OBS in OBS",
             L"The files were installed, but your OBS settings couldn't be updated.\n\n"
             L"To finish by hand: in OBS open Tools → Scripts, click +, and choose:\n" + script,
             TD_WARNING_ICON);
        return 1;
    }

    std::wstring obs = FindObsExe();
    std::wstring body =
        L"Start the Replay Buffer in OBS and save a replay — a “Clip Saved” pop-up will slide in "
        L"at the top right of your screen.\n\n"
        L"To change settings or test it: in OBS open Tools → Scripts and click nova_clip_notify.lua.\n\n"
        L"Tip: games must run in Borderless or Windowed mode for the pop-up to show on top.";
    std::vector<Button> buttons;
    if (!obs.empty()) buttons.push_back({1001, L"Open OBS"});
    buttons.push_back({IDCLOSE, L"Close"});
    int r = Ask(update ? L"Nova OBS has been updated!" : L"Nova OBS is installed!", body.c_str(), buttons, 0, false);
    if (r == 1001 && !g_silent) {
        std::wstring dir = obs.substr(0, obs.find_last_of(L'\\'));
        ShellExecuteW(nullptr, L"open", obs.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
    }
    return 0;
}

static int Uninstall(const Paths& p) {
    if (!WaitForObsClosed()) return 1;

    for (auto& f : SceneCollections(p.scenesDir)) EditCollection(f, std::string());

    DeleteWithRetry(p.installDir + L"\\" + kScriptName);
    DeleteWithRetry(p.installDir + L"\\" + kOverlayName);
    if (p.registry) RegDeleteKeyW(HKEY_CURRENT_USER, kUninstallKey);

    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring setupCopy = p.installDir + L"\\" + kSetupName;
    if (_wcsicmp(self, setupCopy.c_str()) == 0) {
        // We are running from the install folder: remove it shortly after we exit.
        std::wstring cmd = L"cmd.exe /c ping 127.0.0.1 -n 3 >nul & rmdir /s /q \"" + p.installDir + L"\"";
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    } else {
        DeleteWithRetry(setupCopy);
        RemoveDirectoryW(p.installDir.c_str());
    }

    Info(L"Nova OBS has been removed", L"The Clip Saved pop-up is gone from OBS. Your OBS scenes and settings are untouched.");
    return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL(WINAPI * SetCtxFn)(HANDLE);
    if (auto setCtx = (SetCtxFn)GetProcAddress(u32, "SetProcessDpiAwarenessContext")) setCtx((HANDLE)-4);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    bool uninstall = false;
    Paths p;
    p.installDir = KnownFolder(FOLDERID_LocalAppData) + L"\\NovaOBS";
    p.scenesDir = KnownFolder(FOLDERID_RoamingAppData) + L"\\obs-studio\\basic\\scenes";

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--uninstall") == 0) uninstall = true;
        else if (_wcsicmp(argv[i], L"--test-root") == 0 && i + 1 < argc) {
            std::wstring root = argv[++i];
            p.installDir = root + L"\\install";
            p.scenesDir = root + L"\\scenes";
            p.registry = false;
            g_silent = true;
        } else if (_wcsicmp(argv[i], L"--portable-root") == 0 && i + 1 < argc) {
            std::wstring root = argv[++i];
            p.installDir = root + L"\\data\\obs-plugins\\frontend-tools\\scripts";
            p.scenesDir = root + L"\\config\\obs-studio\\basic\\scenes";
            p.registry = false;
            g_silent = true;
        }
    }
    if (argv) LocalFree(argv);

    if (uninstall) {
        if (Ask(L"Uninstall Nova OBS?", L"This removes the Clip Saved pop-up from OBS.",
                {{IDYES, L"Uninstall"}, {IDCANCEL, L"Cancel"}}, 0, false) != IDYES)
            return 0;
        return Uninstall(p);
    }

    bool installed = FileExists(p.installDir + L"\\" + kScriptName);
    if (installed) {
        int r = Ask(L"Nova OBS is already installed",
                    L"What would you like to do?",
                    {{1, L"Update / repair\nReinstall the latest version and keep your settings."},
                     {2, L"Uninstall\nRemove the Clip Saved pop-up from OBS."}},
                    TDCBF_CANCEL_BUTTON, true);
        if (r == 1) return Install(p, true);
        if (r == 2) return Uninstall(p);
        return 0;
    }

    int r = Ask(L"Install Nova OBS",
                L"Adds a “Clip Saved” pop-up with a thumbnail to OBS, shown whenever you save a replay "
                L"— like NVIDIA's overlay.\n\nNo admin rights needed. You can uninstall it any time from "
                L"Windows Settings → Apps.",
                {{1, L"Install"}}, TDCBF_CANCEL_BUTTON, false);
    return r == 1 ? Install(p, false) : 0;
}
