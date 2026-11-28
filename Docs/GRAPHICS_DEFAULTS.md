# Automatic graphics defaults — 0.2.1.1

The plugin applies these local rendering settings at GameInstance startup and local PlayerController BeginPlay, including MainMenu, OpenWorld and return travel:

| Setting | Default |
| --- | --- |
| Frame rate limit (`t.MaxFPS`) | 60 FPS |
| Shadows | Low (0) |
| Global illumination | Low (0) |
| Reflections | Low (0) |
| Post-processing | Low (0) |
| Effects | Medium (1) |

Open the normal .uproject, rebuild and package normally. Every rebuilt client and listen host applies the defaults automatically. Existing installed binaries must be rebuilt to receive code changes. The full project, plugin and original assets are included.

Project Settings > Plugins > Backyard Monsters NXT > Graphics contains `Apply Optimized Graphics Defaults` (enabled) and `Frame Rate Limit` (60). The numeric cap is bounded to 15–240; nonfinite values fall back to 60. Restart the game after changing these project defaults. The preset applies on each game launch/map entry rather than editing user save data. Disable it before introducing a separate video-settings system. Explicit console/command-line overrides retain their normal higher priority.

This sets a rendering cap, not a fixed simulation timestep. Dedicated-server and non-rendering instances skip it. The process is not suspended when unfocused, preserving listen-server operation. No gameplay tick rates, bot counts, networking, accounts or save formats change. Texture quality, screen percentage, resolution/window mode, antialiasing, VSync and Custom Depth are not changed. It does not introduce a new runtime settings menu.

Source/asset validation and an independent integration review were performed. Unreal Engine is unavailable here; native Windows compilation, packaging, GPU temperatures and actual FPS remain unmeasured. A cap limits excess rendering above the target; a machine unable to reach 60 FPS may still be GPU-bound.
