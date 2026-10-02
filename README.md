# Nova OBS

Nova OBS is a Windows build of [OBS Studio](https://github.com/obsproject/obs-studio) for gaming, clips, and streaming. It keeps OBS's full feature set and adds a graphite interface with violet accents and the existing clip-saved notification.

## Download and launch

Download **NovaOBS-32.2.2-Windows-Setup.exe** from a Nova OBS release, run it, and open **Nova OBS** from the Start menu. The installer includes OBS, Nova's theme and clip tools, and the Microsoft C++ runtimes. Users do not need to install Git, CMake, Visual Studio, or OBS separately. The normal Windows permission prompt may appear if a C++ runtime needs installing. This is a 64-bit Windows 10/11 app.

The install opens directly into a Nova Gaming scene and profile at 60 FPS, with preview off and a 20-second replay buffer ready to start. The clip script loads on first launch. The installer registers Virtual Camera when needed, using the normal Windows permission prompt. Add a Game Capture source and set your stream account or key, encoder, and replay hotkey in OBS. Every OBS source and streaming control remains available. The installer keeps Nova's settings separate from an existing OBS installation and preserves scenes and settings when Nova is updated or uninstalled. The download is currently unsigned, so Windows may ask you to confirm that you trust it.

The download packages the official OBS Studio 32.2.2 executable with Nova's theme and clip notification. A separate source build recipe below applies the native Nova patches; it requires development tools only on the build machine.

The build is pinned to OBS Studio **32.2.2**. The upstream source is fetched by `build-nova.ps1` and patched with the small diff in `patches/`. This repository does not copy thousands of OBS source files or prebuilt dependencies into Git.

## What changes

- **Full OBS features:** browser sources, game/window/display capture, streaming services, audio and video filters, WebSocket, virtual camera, scripting, and hardware encoders remain in the build.
- **Gaming defaults for new Nova profiles:** 60 FPS, replay buffer configured, and preview disabled initially. You can turn preview back on from the preview context menu. OBS starts the replay buffer only when you click **Start Replay Buffer** or configure a hotkey.
- **Hardware encoding:** OBS already chooses NVIDIA NVENC for simple output when available, with a software fallback. Nova retains that behavior rather than forcing an encoder that may not exist on another PC.
- **Clip notifications:** the Lua script is built in and appears in **Tools > Scripts**. The thumbnail is off by default because an extra screenshot at each save costs work. You can turn it on there.
- **Separate settings:** the packaged app runs in OBS portable mode, so it does not overwrite the settings of an existing OBS install.

Keeping every feature means the package includes Chromium for browser sources and the other OBS modules. That increases download and disk size. A smaller install would require removing features you asked to keep. Lower runtime use depends on the scene, encoder, resolution, frame rate, and game load; no build can guarantee the lowest possible usage on every PC.

OBS's platform account connections for Twitch, YouTube, and Restream need application credentials that OBS does not publish in source. The source-built variant cannot include those private credentials. Streaming to those services by stream key remains available.

## Build the one-file download

On a build machine with OBS Studio 32.2.2 installed, NSIS 3.13 unpacked in `.nova-build/nsis-3.13`, and the Microsoft x64 and x86 C++ redistributable installers in `.nova-build/redist`, run `./build-download.ps1`. The output is `dist/NovaOBS-32.2.2-Windows-Setup.exe`. The build checks both Microsoft signatures and stages a clean, new configuration. `-SkipOverlayBuild` reuses the repository's current Nova executables. These tools are only for making a release; the download contains what users need.

## Build on Windows

You need Windows 10 or 11, Visual Studio 2022 C++ Build Tools with **Desktop development with C++** and **ATL**, the Windows SDK, Git, CMake 3.28+, internet access, and enough free disk space for OBS, Qt, and Chromium dependencies.

Run in PowerShell:

```powershell
.\build-nova.ps1
```

The output is `.nova-build\NovaOBS`. Open **Launch Nova OBS.cmd** in that folder. The first build downloads OBS's pinned dependencies and takes longer; later builds reuse them. `-PrepareOnly` only fetches and applies the source changes. `-SkipOverlayBuild` reuses the current `NovaOverlay.exe`.

The build script checks the exact upstream commit before applying the patch. The output includes the OBS GPL license and `NOVA-SOURCE.txt` identifying the source. Distributing a modified OBS binary requires following OBS's GPL obligations and providing the corresponding source and changes.

### Portable preview from an installed OBS 32.2.2

If ATL is not installed yet, run `./package-portable-preview.ps1` in PowerShell. It copies an existing OBS Studio 32.2.2 installation into `.nova-build\NovaOBS-preview`, adds the Nova theme and clip notification, and keeps its settings separate. Open **Launch Nova OBS.cmd** there. Add `-SeedScene` to activate the clip script on the first launch. This preview uses the official OBS executable; the compiled Nova title and new profile defaults require the source build above.

## Set up clips and streaming

1. Create a scene and add **Game Capture** for the game. Add an audio source if needed. OBS describes Game Capture as its most efficient game source.
2. Open **Settings > Output** and confirm the encoder, stream bitrate, replay duration, recording quality, and save location for your hardware and platform.
3. Set a **Save Replay** hotkey in **Settings > Hotkeys**. Start the replay buffer, then save a clip to see the Nova notification.
4. To show a clip thumbnail, open **Tools > Scripts > nova_clip_notify.lua** and enable it.
5. If frames drop, check **View > Stats**. Limit the game's frame rate or reduce capture output before raising OBS process priority. OBS's [encoding performance guide](https://obsproject.com/kb/encoding-performance-troubleshooting) explains the GPU headroom tradeoff.

The one-file installer handles **Virtual Camera** registration when needed. If you use only the manually assembled portable preview, run `data\obs-plugins\win-dshow\virtualcam-install.bat` as Administrator once. OBS documents this [portable-mode registration step](https://obsproject.com/kb/virtual-camera-troubleshooting).

## Existing clip notification installer

`NovaOBS-Setup.exe` is still the separate installer for people who want only the notification in a regular OBS install. It is **not** the Nova OBS application. It installs the Lua script and overlay into an existing OBS configuration. See `src\build.bat` to rebuild those two small executables. For manual use, keep `nova_clip_notify.lua` and `NovaOverlay.exe` together and add the Lua file in **Tools > Scripts**.

The notification uses a click-through Windows window. It does not inject into the game. It appears over borderless and windowed games; exclusive fullscreen can cover it. Errors are logged at `%TEMP%\nova_obs_overlay.log`.

## Sources and licenses

- [OBS Studio source and GPL license](https://github.com/obsproject/obs-studio)
- [OBS theme system](https://github.com/obsproject/obs-studio/wiki/OBS-Studio-Theme-System)
- [OBS Windows build instructions](https://github.com/obsproject/obs-studio/wiki/Build-Instructions-For-Windows)

The original Nova notification code in this repository is MIT licensed (`LICENSE`). OBS Studio and a modified OBS build remain subject to OBS's GPL license.
