# Known issues

State of the PS Vita build as of 17 November 2026 (version 0.0.2). "Console" means
seen on a real PS Vita; "PC" means the Linux test build only.

## Open

| Issue | Where | Notes |
|---|---|---|
| Lens flares not hidden by walls | Console | Lens flares and halos are missing in 0.0.2 and fixed in the source after it. The fix skips the game's visibility test (as its own `lensflare_ztest` setting does), so a light behind an obstacle may keep its flare. `SH3_LENSFORCE=0` in env.txt restores the test. |
| Performance: 20 to 30 fps depending on the area | Console | The 30 fps cap holds in lighter areas; heavier ones need ~35–47 ms of game code per frame and run at about 20 fps. Loading spots drop briefly. |
| Movies at about 20 fps from the original `.000` files | Console | The game copies every frame into its 512x512 movie texture and the port re-uploads it each frame. Convert the movies to MP4 (README, Install): the hardware decoder plays them at 30 fps. |
| MP4 movies: fades drawn under the video | Console | Fades and overlays the game draws over a movie end up under the MP4 video. The MP4 player keeps 32 MB of video memory aside. |
| Jagged edges | Console | No anti-aliasing: everything is drawn into off-screen buffers without multisampling. |
| Room sound effects play "dry" | Console | Footsteps, doors and items play, but the DirectSound effect the game asks for on them is not emulated. |
| Most of the game | Console | Only the opening and the first areas have been played. Other movies, endings, later areas and long sessions are unconfirmed. |

## Confirmed on the console (15 November 2026)

- Movies at 30 fps through the MP4 path.
- The credits page before the opening movie, and the title menu without Exit.
- The Vita button layout described in the README.
- In-game sound effects (footsteps, doors, items).
- Saving and loading a game.

## Confirmed on the console (17 November 2026)

- The dark square over Heather's flashlight is gone (the flare's 1-pixel visibility point was drawn at an undefined size on the Vita).
- The map shows Heather's arrow and the page arrows again.

## Confirmed on the console (24 November 2026, after 0.0.2)

- Lens flares and light halos are drawn, in the flashlight cutscene.

## Things to know

- **Requirements:** a Vita with custom firmware, `ur0:data/libshacccg.suprx`, and the 2003 PC release of the game (`sh3.exe` SHA-256 `8ff8f748…0918`). The package sets the extended memory budget; without it the game runs out of memory.
- **Controls file:** if the game has written `ux0:data/sh3-vita/key.ini`, its bindings replace the port's layout. Delete it to get the default layout back.
- **Resolution:** the game always renders at 960x544; the Options resolution row shows only that mode.
- **First launch:** the shaders are compiled on the console the first time each one is needed, so the first minutes stutter more than later runs (the compiled shaders are kept in `ux0:data/sh3-vita/shc-r1`).
- **Logs:** `ux0:data/sh3-vita/sh3log.txt`. Include it when reporting a problem.
- **Vita3K:** the emulator is useful for some checks but does not behave like the console here; results from it are not console results.
