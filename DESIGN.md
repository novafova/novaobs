# Nova OBS interface

Nova is OBS in a dark gaming workspace. The preview and scene controls stay where OBS users expect them; the brand is carried by graphite surfaces, restrained violet focus and selection, and the clip notification. Red recording and other OBS status colors keep their functional meaning.

## Native theme

`nova-theme/Nova_Gaming.ovt` extends OBS 32.2.2's Yami base. It overrides color variables only. This preserves the upstream control layout, keyboard focus, accessibility behavior, source icons, and plugin dialogs. There are no gradients, halos, glass, ornamental rules, icon tiles, or entrance animations. Controls remain visible by default.

| Role | Color |
|---|---|
| Window | `#211F27` |
| Work area | `#292630` |
| Preview backdrop | `#17161C` |
| Primary violet | `#6C539B` |
| Focus violet | `#AA91DC` |
| Main text | `#F5F2F9` |
| Secondary text | `#BDB6C8` |

The violet is tonal rather than neon. The selected control is legible at 5.66:1 white-on-violet. Main text is 14.69:1, muted text is 8.29:1, and focus violet is 6.04:1 on the window color.

## Clip notification

`src/nova_overlay.cpp` keeps a compact screenshot/clip card. Its check mark is bare, the violet accent matches the theme, and its shadow is tight and directional. Text has an internal gutter and long filenames are shortened with an ellipsis. The thumbnail is optional and disabled by default to avoid an extra screenshot on every saved replay. The notification appears only for a saved clip and has no background process between clips.

## Anti-slop review

- **Composition and typography:** The native OBS layout remains a task tool, with no landing-page hero, card grid, testimonial, pricing block, or giant footer wordmark. No trendy display font or decorative metadata treatment was added.
- **Color and depth:** One graphite/violet system carries the UI. No blue-purple gradient, clipped glow, radial halo, candy background, generic cream/gray base, fake shadow box, or grain over content was added. The overlay shadow was reduced.
- **Controls and behavior:** OBS's real controls and iconography are retained. No faux controls, floating UI props, hover boops, growing underlines, sun/moon switch, or reveal-gated content was introduced. The clip check mark no longer sits inside a colored circle.
- **Readability:** The theme contrast ratios above exceed 4.5:1 for the tested text roles. The clip card keeps text clear of its edges, uses ellipsis for long subtitles, and keeps the mark and title separate.
- **Performance:** The default preview is off, NVENC remains OBS's default when present, and notification screenshots are optional. This does not promise a universal CPU/GPU minimum: 60 FPS and complex scenes still cost resources.

The downloadable installer was installed and launched in a clean test directory. The selected collection and profile were `Nova Gaming`, the log showed 60 FPS, the clip script loaded on the first launch, and the OBS browser, WebSocket, and NVENC modules loaded. The theme and preview setting were seeded in OBS's `user.ini`; no appearance control depends on an animation or an extra service. A window capture confirmed the native layout, purple selection, visible replay control, and disabled preview with an explicit Enable Preview button. Reinstalling preserved the scene file byte for byte, and uninstalling kept the user's config. I rechecked the supplied anti-slop rules against the actual work: the web page patterns do not apply to OBS's native workspace, and the applicable color, control, type, clipping, alignment, contrast, and motion checks above remain satisfied. The modified native OBS source build still needs Visual Studio's ATL component before it can be compiled and visually inspected.
