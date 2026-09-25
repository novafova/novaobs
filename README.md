# Nova OBS — Clip Saved Notifications

When you save a replay (clip) in OBS, a small card slides in at the top right of your screen. It says **Clip Saved** and shows a thumbnail of the clip, the way NVIDIA's overlay does. You can turn it on or off from inside OBS.

## Install (easy way)
1. **[Download NovaOBS-Setup.exe](https://github.com/novafova/novaobs/raw/main/NovaOBS-Setup.exe)**
2. Close OBS.
3. Double-click `NovaOBS-Setup.exe` and click **Install**.
4. Open OBS, start the Replay Buffer, and save a replay.

If Windows says **"Windows protected your PC"**, click **More info → Run anyway**. Windows shows this for any app from a small developer that hasn't paid for a code-signing certificate. All the source code is in this repo.

The installer doesn't need admin rights. To remove Nova OBS, go to **Windows Settings → Apps**, or run the installer again.
You need Windows 10 or 11 and OBS 28 or newer.

## Install (manual way)
1. Click the green **Code** button, choose **Download ZIP**, and extract it to a folder you'll keep.
2. Keep `nova_clip_notify.lua` and `NovaOverlay.exe` in the same folder.
3. In OBS, open **Tools → Scripts**, click **+** and choose `nova_clip_notify.lua`.
4. Click **Show test notification** to check that it works.

## Settings (Tools → Scripts → nova_clip_notify.lua)
| Setting | What it does |
|---|---|
| Show "Clip Saved" notification | Turns the feature on or off |
| Position | Top right (default), top left, bottom right or bottom left |
| Display time | How long the card stays on screen |
| Size | Changes the card size (it already adjusts to your display scaling) |
| Show clip thumbnail | Shows the frame from the moment you saved |
| Play sound | Plays the Windows notification sound |
| Hide notification from recordings/stream | Keeps the card out of your recordings and stream |

## Performance and anti-cheat
- **No injection or hooks.** It never touches the game process, so anti-cheat (Vanguard, EAC, BattlEye) has nothing to detect. The card is a normal always-on-top window.
- **No background process.** Nothing runs between clips. Each clip starts `NovaOverlay.exe` for about 5 seconds, and then it closes itself.
- **Light on the CPU.** It uses about 35 ms of CPU once at startup, at below-normal priority, so the game always gets the CPU first. The slide animation is timed to your monitor's refresh rate. While the card sits still, it uses no CPU at all.
- **Doesn't change system timer settings,** which is a common cause of stutter.
- **Never steals focus.** It is click-through and doesn't take focus, so your game won't minimize.
- The card appears on the monitor you're playing on. Games need to run in **borderless** or **windowed** mode for it to show; games in true exclusive fullscreen draw over every window.

## Building it yourself
Run `src\build.bat`. It needs the Visual Studio 2022 C++ Build Tools.
It builds `NovaOverlay.exe` first, then `NovaOBS-Setup.exe`, which contains the overlay and the Lua script.

If something goes wrong, check the error log at `%TEMP%\nova_obs_overlay.log`.
