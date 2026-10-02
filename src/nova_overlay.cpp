// Nova OBS - "Clip Saved" toast overlay (native, no dependencies).
//
// Launched by nova_clip_notify.lua each time the replay buffer is saved.
// Draws a card with a thumbnail of the clip, slides it in from the screen edge,
// holds it, slides it out and exits.
//
// Performance / safety design:
//  * Never touches the game process (no injection or hooks), so anti-cheat has
//    nothing to see. It is an ordinary always-on-top window.
//  * Runs at below-normal priority so game threads always win the CPU.
//  * The card is rendered once up front; animation frames are a small memcpy
//    plus one UpdateLayeredWindow call, paced to the display refresh.
//  * While the card is holding still, the process sleeps in a kernel wait
//    (0% CPU). It never calls timeBeginPeriod (no global timer changes).
//  * Click-through, never activates, never steals focus from the game.
//  * Excluded from screen capture so it doesn't show up in recordings.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
using std::max;
using std::min;
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <mmsystem.h>

#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace Gdiplus;

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

static const wchar_t* kClassName = L"NovaOBSClipToast";
static const Color kAccent(255, 157, 130, 214);
static const BYTE kBgR = 27, kBgG = 25, kBgB = 32;

static volatile bool g_quit = false;

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

static void Log(const wchar_t* fmt, ...) {
    wchar_t path[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, path)) return;
    wcscat_s(path, L"nova_obs_overlay.log");
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"a, ccs=UTF-8") != 0 || !f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fwprintf(f, L"%04d-%02d-%02d %02d:%02d:%02d ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    va_list ap;
    va_start(ap, fmt);
    vfwprintf(f, fmt, ap);
    va_end(ap);
    fputwc(L'\n', f);
    fclose(f);
}

static void DeleteFileRetry(const std::wstring& path) {
    for (int i = 0; i < 20; ++i) {
        if (DeleteFileW(path.c_str())) return;
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) return;
        Sleep(100);
    }
}

static std::wstring BaseName(const std::wstring& p) {
    size_t i = p.find_last_of(L"/\\");
    return i == std::wstring::npos ? p : p.substr(i + 1);
}

static double Now() {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) / double(freq.QuadPart);
}

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

struct Options {
    std::wstring clip, screenshot, position = L"top-right", dumpRaw;
    bool deleteScreenshot = false, sound = false, noThumbnail = false, noHide = false, test = false;
    double duration = 4.0, scale = 100.0;
};

static Options ParseArgs() {
    Options o;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return o;
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        bool hasNext = i + 1 < argc;
        if (a == L"--clip" && hasNext) o.clip = argv[++i];
        else if (a == L"--screenshot" && hasNext) o.screenshot = argv[++i];
        else if (a == L"--position" && hasNext) o.position = argv[++i];
        else if (a == L"--duration" && hasNext) o.duration = _wtof(argv[++i]);
        else if (a == L"--scale" && hasNext) o.scale = _wtof(argv[++i]);
        else if (a == L"--delete-screenshot") o.deleteScreenshot = true;
        else if (a == L"--sound") o.sound = true;
        else if (a == L"--no-thumbnail") o.noThumbnail = true;
        else if (a == L"--no-hide-from-capture") o.noHide = true;
        else if (a == L"--test") o.test = true;
        else if (a == L"--dump-raw" && hasNext) o.dumpRaw = argv[++i];
    }
    LocalFree(argv);
    if (!(o.duration > 0.0)) o.duration = 4.0;
    o.duration = min(max(o.duration, 0.5), 60.0);
    if (!(o.scale > 0.0)) o.scale = 100.0;
    o.scale = min(max(o.scale, 25.0), 400.0);
    if (o.position != L"top-right" && o.position != L"top-left" &&
        o.position != L"bottom-right" && o.position != L"bottom-left")
        o.position = L"top-right";
    return o;
}

// ---------------------------------------------------------------------------
// DPI / monitor
// ---------------------------------------------------------------------------

static void EnableDpiAwareness() {
    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL(WINAPI * SetCtxFn)(HANDLE);
    auto setCtx = (SetCtxFn)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
    if (setCtx && setCtx((HANDLE)-4)) return;  // per-monitor v2
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        typedef HRESULT(WINAPI * SetAwFn)(int);
        auto setAw = (SetAwFn)GetProcAddress(shcore, "SetProcessDpiAwareness");
        if (setAw && SUCCEEDED(setAw(2))) return;
    }
    SetProcessDPIAware();
}

static double MonitorScale(HMONITOR mon) {
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        typedef HRESULT(WINAPI * GetDpiFn)(HMONITOR, int, UINT*, UINT*);
        auto getDpi = (GetDpiFn)GetProcAddress(shcore, "GetDpiForMonitor");
        UINT dx = 96, dy = 96;
        if (getDpi && SUCCEEDED(getDpi(mon, 0, &dx, &dy)) && dx > 0) return dx / 96.0;
    }
    HDC dc = GetDC(nullptr);
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    return (dpi > 0 ? dpi : 96) / 96.0;
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void AddRoundRect(GraphicsPath& p, REAL x, REAL y, REAL w, REAL h, REAL r) {
    REAL d = r * 2;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

static void SetQuality(Graphics& g) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g.SetCompositingQuality(CompositingQualityHighQuality);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
}

// Loads an image file fully into memory (so the file isn't locked) and returns
// a tw x th cover-fitted copy. Retries while OBS may still be writing it.
static Bitmap* LoadThumbnail(const std::wstring& path, int tw, int th) {
    for (int attempt = 0; attempt < 60; ++attempt) {
        if (attempt) Sleep(50);
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        LARGE_INTEGER size{};
        std::vector<BYTE> buf;
        bool ok = false;
        if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (1LL << 30)) {
            buf.resize((size_t)size.QuadPart);
            DWORD read = 0;
            ok = ReadFile(h, buf.data(), (DWORD)buf.size(), &read, nullptr) && read == buf.size();
        }
        CloseHandle(h);
        if (!ok) continue;

        IStream* stream = SHCreateMemStream(buf.data(), (UINT)buf.size());
        if (!stream) continue;
        Bitmap* src = Bitmap::FromStream(stream);
        Bitmap* out = nullptr;
        if (src && src->GetLastStatus() == Ok && src->GetWidth() > 0 && src->GetHeight() > 0) {
            REAL sw = (REAL)src->GetWidth(), sh = (REAL)src->GetHeight();
            REAL target = (REAL)tw / (REAL)th;
            REAL cx = 0, cy = 0, cw = sw, ch = sh;
            if (sw / sh > target) { cw = sh * target; cx = (sw - cw) / 2; }
            else { ch = sw / target; cy = (sh - ch) / 2; }
            out = new Bitmap(tw, th, PixelFormat32bppPARGB);
            Graphics g(out);
            SetQuality(g);
            ImageAttributes attr;
            attr.SetWrapMode(WrapModeTileFlipXY);  // no dark fringe at the edges
            Status s = g.DrawImage(src, RectF(0, 0, (REAL)tw, (REAL)th), cx, cy, cw, ch, UnitPixel, &attr);
            if (s != Ok) { delete out; out = nullptr; }
        }
        delete src;
        stream->Release();
        if (out) return out;
    }
    Log(L"could not load screenshot: %s", path.c_str());
    return nullptr;
}

static Bitmap* PlaceholderThumbnail(int tw, int th) {
    Bitmap* bmp = new Bitmap(tw, th, PixelFormat32bppPARGB);
    Graphics g(bmp);
    SetQuality(g);
    LinearGradientBrush grad(PointF(0, 0), PointF((REAL)tw, (REAL)th),
                             Color(255, 76, 58, 150), Color(255, 34, 32, 52));
    g.FillRectangle(&grad, 0, 0, tw, th);
    REAL cx = tw / 2.0f, cy = th / 2.0f, r = th / 5.0f;
    PointF tri[3] = {PointF(cx - r * 0.7f, cy - r), PointF(cx - r * 0.7f, cy + r), PointF(cx + r, cy)};
    SolidBrush white(Color(255, 255, 255, 255));
    g.FillPolygon(&white, tri, 3);
    return bmp;
}

static Font* MakeFont(const wchar_t* family, const wchar_t* fallback, REAL px, int style) {
    FontFamily fam(family);
    if (fam.GetLastStatus() == Ok) {
        Font* f = new Font(&fam, px, style, UnitPixel);
        if (f->GetLastStatus() == Ok) return f;
        delete f;
    }
    FontFamily fb(fallback);
    if (fb.GetLastStatus() == Ok) return new Font(&fb, px, style, UnitPixel);
    return new Font(FontFamily::GenericSansSerif(), px, style, UnitPixel);
}

// Box blur of an 8-bit alpha buffer (3 passes approximates a Gaussian).
static void BoxBlur(std::vector<int>& a, int w, int h, int r) {
    std::vector<int> tmp(a.size());
    int win = 2 * r + 1;
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {
            const int* row = &a[(size_t)y * w];
            int sum = 0;
            for (int x = -r; x <= r; ++x) sum += (x >= 0 && x < w) ? row[x] : 0;
            for (int x = 0; x < w; ++x) {
                tmp[(size_t)y * w + x] = sum / win;
                int add = x + r + 1, sub = x - r;
                sum += (add < w ? row[add] : 0) - (sub >= 0 ? row[sub] : 0);
            }
        }
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            for (int y = -r; y <= r; ++y) sum += (y >= 0 && y < h) ? tmp[(size_t)y * w + x] : 0;
            for (int y = 0; y < h; ++y) {
                a[(size_t)y * w + x] = sum / win;
                int add = y + r + 1, sub = y - r;
                sum += (add < h ? tmp[(size_t)add * w + x] : 0) - (sub >= 0 ? tmp[(size_t)sub * w + x] : 0);
            }
        }
    }
}

struct Card {
    int w = 0, h = 0, pad = 0;
    std::vector<uint32_t> px;  // premultiplied BGRA, top-down
};

static Card BuildCard(double scale, Bitmap* thumbSrc, const std::wstring& subtitle) {
    auto S = [scale](double v) { return max(1, (int)lround(v * scale)); };
    const int cw = S(360), ch = S(88), pad = S(16);
    const int iw = cw + 2 * pad, ih = ch + 2 * pad;
    const REAL radius = (REAL)S(12);

    Card card;
    card.w = iw;
    card.h = ih;
    card.pad = pad;

    // --- soft drop shadow ---
    std::vector<int> alpha((size_t)iw * ih, 0);
    {
        Bitmap mask(iw, ih, PixelFormat32bppPARGB);
        {
            Graphics g(&mask);
            SetQuality(g);
            GraphicsPath p;
            AddRoundRect(p, (REAL)pad, (REAL)(pad + S(4)), (REAL)cw, (REAL)ch, radius);
            SolidBrush black(Color(255, 0, 0, 0));
            g.FillPath(&black, &p);
        }
        BitmapData bd;
        Rect r(0, 0, iw, ih);
        if (mask.LockBits(&r, ImageLockModeRead, PixelFormat32bppPARGB, &bd) == Ok) {
            for (int y = 0; y < ih; ++y) {
                const uint32_t* row = (const uint32_t*)((BYTE*)bd.Scan0 + (size_t)y * bd.Stride);
                for (int x = 0; x < iw; ++x) alpha[(size_t)y * iw + x] = row[x] >> 24;
            }
            mask.UnlockBits(&bd);
        }
        BoxBlur(alpha, iw, ih, max(1, S(3)));
    }

    Bitmap out(iw, ih, PixelFormat32bppPARGB);
    {
        BitmapData bd;
        Rect r(0, 0, iw, ih);
        if (out.LockBits(&r, ImageLockModeWrite, PixelFormat32bppPARGB, &bd) == Ok) {
            for (int y = 0; y < ih; ++y) {
                uint32_t* row = (uint32_t*)((BYTE*)bd.Scan0 + (size_t)y * bd.Stride);
                for (int x = 0; x < iw; ++x) row[x] = (uint32_t)(alpha[(size_t)y * iw + x] * 74 / 255) << 24;
            }
            out.UnlockBits(&bd);
        }
    }

    // --- card ---
    {
        Graphics g(&out);
        SetQuality(g);
        const REAL ox = (REAL)pad, oy = (REAL)pad;

        GraphicsPath body;
        AddRoundRect(body, ox, oy, (REAL)cw, (REAL)ch, radius);
        SolidBrush bg(Color(247, kBgR, kBgG, kBgB));
        g.FillPath(&bg, &body);

        // Thumbnail with antialiased rounded corners (texture brush, not clip).
        const int tw = S(128), th = S(72);
        const REAL tx = ox + S(8), ty = oy + (REAL)((ch - th) / 2);
        Bitmap* thumb = thumbSrc ? thumbSrc : PlaceholderThumbnail(tw, th);
        {
            // The thumbnail is already exactly tw x th, so map it 1:1 (no resampling).
            TextureBrush tb(thumb, WrapModeClamp);
            tb.TranslateTransform(tx, ty);
            GraphicsPath tp;
            AddRoundRect(tp, tx, ty, (REAL)tw, (REAL)th, (REAL)S(7));
            g.SetInterpolationMode(InterpolationModeNearestNeighbor);
            g.FillPath(&tb, &tp);
            g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            Pen edge(Color(28, 255, 255, 255), 1.0f);
            g.DrawPath(&edge, &tp);
        }
        if (!thumbSrc) delete thumb;

        const REAL textX = tx + tw + S(14);
        const REAL avail = ox + cw - textX - S(14);

        StringFormat fmt(StringFormatFlagsNoWrap);
        fmt.SetAlignment(StringAlignmentNear);
        fmt.SetLineAlignment(StringAlignmentCenter);
        fmt.SetTrimming(StringTrimmingEllipsisCharacter);

        Font* fLabel = MakeFont(L"Segoe UI Semibold", L"Arial", (REAL)S(10), FontStyleRegular);
        Font* fTitle = MakeFont(L"Segoe UI", L"Arial", (REAL)S(16), FontStyleBold);
        Font* fSub = MakeFont(L"Segoe UI", L"Arial", (REAL)S(12), FontStyleRegular);

        SolidBrush accent(kAccent);
        SolidBrush titleBrush(Color(255, 245, 246, 250));
        SolidBrush subBrush(Color(255, 188, 182, 197));

        auto line = [&](REAL cy, REAL h) { return RectF(textX, oy + cy - h / 2, avail, h); };

        g.DrawString(L"NOVA OBS  •  REPLAY", -1, fLabel, line((REAL)S(21), (REAL)S(16)), &fmt, &accent);

        // Bare check mark, kept clear of the title's text bounds.
        const REAL d = (REAL)S(18);
        const REAL ix = textX, iy = oy + S(43) - d / 2;
        Pen check(kAccent, max(1.5f, d * 0.11f));
        check.SetStartCap(LineCapRound);
        check.SetEndCap(LineCapRound);
        check.SetLineJoin(LineJoinRound);
        PointF pts[3] = {PointF(ix + d * 0.27f, iy + d * 0.52f), PointF(ix + d * 0.44f, iy + d * 0.68f),
                         PointF(ix + d * 0.74f, iy + d * 0.35f)};
        g.DrawLines(&check, pts, 3);

        RectF titleRect = line((REAL)S(43), (REAL)S(24));
        titleRect.X += d + S(8);
        titleRect.Width -= d + S(8);
        g.DrawString(L"Clip Saved", -1, fTitle, titleRect, &fmt, &titleBrush);

        g.DrawString(subtitle.c_str(), -1, fSub, line((REAL)S(66), (REAL)S(18)), &fmt, &subBrush);

        delete fLabel;
        delete fTitle;
        delete fSub;

        GraphicsPath border;
        AddRoundRect(border, ox + 0.5f, oy + 0.5f, (REAL)cw - 1, (REAL)ch - 1, radius - 0.5f);
        Pen hairline(Color(30, 255, 255, 255), 1.0f);
        g.DrawPath(&hairline, &border);
    }

    card.px.assign((size_t)iw * ih, 0);
    BitmapData bd;
    Rect r(0, 0, iw, ih);
    if (out.LockBits(&r, ImageLockModeRead, PixelFormat32bppPARGB, &bd) == Ok) {
        for (int y = 0; y < ih; ++y)
            memcpy(&card.px[(size_t)y * iw], (BYTE*)bd.Scan0 + (size_t)y * bd.Stride, (size_t)iw * 4);
        out.UnlockBits(&bd);
    }
    return card;
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:  // a newer toast asked us to go away
        g_quit = true;
        return 0;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void PumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) g_quit = true;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void CloseOtherToasts() {
    HWND h = nullptr;
    while ((h = FindWindowExW(nullptr, h, kClassName, nullptr)) != nullptr) PostMessageW(h, WM_CLOSE, 0, 0);
}

static double EaseOutQuint(double t) { return 1 - pow(1 - t, 5); }
static double EaseInCubic(double t) { return t * t * t; }

static int Run(const Options& opt) {
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    EnableDpiAwareness();

    // Show on the monitor the user is actually looking at (the game's monitor).
    HWND fg = GetForegroundWindow();
    HMONITOR mon = fg ? MonitorFromWindow(fg, MONITOR_DEFAULTTOPRIMARY)
                      : MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof(mi)};
    if (!GetMonitorInfoW(mon, &mi)) {
        mi.rcWork = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    const double scale = MonitorScale(mon) * (opt.scale / 100.0);

    GdiplusStartupInput gsi;
    ULONG_PTR gdipToken = 0;
    if (GdiplusStartup(&gdipToken, &gsi, nullptr) != Ok) {
        Log(L"GDI+ failed to start");
        return 1;
    }

    Card card;
    {
        const bool timing = GetEnvironmentVariableW(L"NOVA_TIMING", nullptr, 0) > 0;
        const double t0 = Now();
        const int tw = max(1, (int)lround(128 * scale)), th = max(1, (int)lround(72 * scale));
        Bitmap* thumb = nullptr;
        if (!opt.noThumbnail && !opt.screenshot.empty()) thumb = LoadThumbnail(opt.screenshot, tw, th);
        if (!opt.screenshot.empty() && opt.deleteScreenshot) DeleteFileRetry(opt.screenshot);
        const double t1 = Now();

        std::wstring subtitle = opt.test ? L"Test notification — it works!"
                              : !opt.clip.empty() ? BaseName(opt.clip)
                                                  : L"Saved to your recordings folder";
        card = BuildCard(scale, thumb, subtitle);
        delete thumb;
        if (timing) Log(L"timing: thumbnail %.1f ms, card %.1f ms", (t1 - t0) * 1000, (Now() - t1) * 1000);
    }
    if (!opt.dumpRaw.empty()) {  // debug: write the rendered card (w, h, premultiplied BGRA) and exit
        FILE* f = nullptr;
        if (_wfopen_s(&f, opt.dumpRaw.c_str(), L"wb") == 0 && f) {
            fwrite(&card.w, 4, 1, f);
            fwrite(&card.h, 4, 1, f);
            fwrite(card.px.data(), 4, card.px.size(), f);
            fclose(f);
        }
        GdiplusShutdown(gdipToken);
        return 0;
    }
    GdiplusShutdown(gdipToken);

    // Card sits `margin` px from the screen edge. The window spans from the card
    // to the edge, so the slide is clipped exactly at the edge of the monitor.
    const int margin = (int)lround(24 * scale);
    const int gap = max(0, margin - card.pad);
    const int winW = card.w + gap, winH = card.h;
    const bool right = opt.position.find(L"right") != std::wstring::npos;
    const bool top = opt.position.rfind(L"top", 0) == 0;
    const int winX = right ? mi.rcWork.right - winW : mi.rcWork.left;
    const int winY = top ? mi.rcWork.top + gap : mi.rcWork.bottom - winH - gap;
    const int restDx = right ? 0 : gap;
    const int hiddenDx = right ? winW : -card.w;

    CloseOtherToasts();

    HINSTANCE hinst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hinst;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    const DWORD ex = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
    HWND hwnd = CreateWindowExW(ex, kClassName, L"Nova OBS Clip Saved", WS_POPUP, winX, winY, winW, winH,
                                nullptr, nullptr, hinst, nullptr);
    if (!hwnd) {
        Log(L"CreateWindowEx failed: %lu", GetLastError());
        return 1;
    }
    if (!opt.noHide) SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);

    HDC screenDC = GetDC(nullptr);
    HDC memDC = CreateCompatibleDC(screenDC);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = winW;
    bmi.bmiHeader.biHeight = -winH;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bitsPtr = nullptr;
    HBITMAP dib = CreateDIBSection(memDC, &bmi, DIB_RGB_COLORS, &bitsPtr, nullptr, 0);
    if (!dib || !bitsPtr) {
        Log(L"CreateDIBSection failed: %lu", GetLastError());
        return 1;
    }
    HGDIOBJ oldBmp = SelectObject(memDC, dib);
    uint32_t* bits = (uint32_t*)bitsPtr;

    auto present = [&](int dx, BYTE alpha, bool redraw) {
        if (redraw) {
            memset(bits, 0, (size_t)winW * winH * 4);
            int x0 = max(0, -dx), x1 = min(card.w, winW - dx);
            if (x1 > x0) {
                for (int y = 0; y < winH; ++y)
                    memcpy(bits + (size_t)y * winW + dx + x0, &card.px[(size_t)y * card.w + x0],
                           (size_t)(x1 - x0) * 4);
            }
        }
        POINT pt{winX, winY}, src{0, 0};
        SIZE sz{winW, winH};
        BLENDFUNCTION bf{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd, screenDC, &pt, &sz, memDC, &src, 0, &bf, ULW_ALPHA);
    };

    // Frame pacing: DwmFlush waits for the next compositor frame (vsync). If it
    // returns instantly (nothing to compose), fall back to a high-res timer so
    // we never spin the CPU.
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
    double framePeriod = 1.0 / 60.0;
    {
        DWM_TIMING_INFO ti{};
        ti.cbSize = sizeof(ti);
        if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti)) && ti.rateRefresh.uiNumerator > 0 &&
            ti.rateRefresh.uiDenominator > 0)
            framePeriod = min(1.0 / 30.0, max(1.0 / 360.0, (double)ti.rateRefresh.uiDenominator / ti.rateRefresh.uiNumerator));
    }
    auto waitFor = [&](double seconds) {
        if (seconds <= 0) return;
        if (timer) {
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)(seconds * 1e7);
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                MsgWaitForMultipleObjects(1, &timer, FALSE, INFINITE, QS_ALLINPUT);
                return;
            }
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, (DWORD)max(1.0, seconds * 1000), QS_ALLINPUT);
    };
    auto waitNextFrame = [&]() {
        double t0 = Now();
        if (SUCCEEDED(DwmFlush()) && Now() - t0 > 0.001) return;
        waitFor(framePeriod - (Now() - t0));
    };

    if (opt.sound) PlaySoundW(L"Notification.Default", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT);

    const double tIn = 0.45, tOut = 0.35, hold = opt.duration;
    const double total = tIn + hold + tOut;

    present(hiddenDx, 0, true);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    int lastDx = INT32_MIN;
    int lastAlpha = -1;
    double lastTopmost = -1;
    const double start = Now();

    while (!g_quit) {
        const double t = Now() - start;
        if (t >= total) break;

        double p, a;
        if (t < tIn) {
            p = EaseOutQuint(t / tIn);
            a = min(1.0, t / (tIn * 0.6));
        } else if (t < tIn + hold) {
            p = 1.0;
            a = 1.0;
        } else {
            double q = (t - tIn - hold) / tOut;
            p = 1.0 - EaseInCubic(q);
            a = 1.0 - q * q;
        }
        const int dx = (int)lround(hiddenDx + (restDx - hiddenDx) * p);
        const int alpha = (int)lround(min(1.0, max(0.0, a)) * 255);
        if (dx != lastDx || alpha != lastAlpha) {
            present(dx, (BYTE)alpha, dx != lastDx);
            lastDx = dx;
            lastAlpha = alpha;
        }
        if (t - lastTopmost > 1.0) {
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            lastTopmost = t;
        }
        PumpMessages();

        if (t >= tIn && t < tIn + hold) {
            // Holding still: sleep in the kernel (0% CPU) until the slide-out starts.
            waitFor(min(1.0, tIn + hold - t));
        } else {
            waitNextFrame();
        }
    }

    ShowWindow(hwnd, SW_HIDE);
    if (timer) CloseHandle(timer);
    SelectObject(memDC, oldBmp);
    DeleteObject(dib);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
    DestroyWindow(hwnd);
    return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    Options opt = ParseArgs();
    int rc = Run(opt);
    if (!opt.screenshot.empty() && opt.deleteScreenshot) DeleteFileRetry(opt.screenshot);
    return rc;
}
