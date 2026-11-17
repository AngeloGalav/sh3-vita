# Known issues

State of the PS Vita build as of 15 November 2026 (version 0.0.1 plus the changes after it). "Console" means
seen on a real PS Vita; "PC" means the Linux test build only.

## Open

| Issue | Where | Notes |
|---|---|---|
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

- Lens flares, including the one from Heather's flashlight: the dark square over the lens is gone (the flare's 1-pixel visibility point was drawn at an undefined size on the Vita).
- The map shows Heather's arrow and the page arrows again.

## Things to know

- **Requirements:** a Vita with custom firmware, `ur0:data/libshacccg.suprx`, and the 2003 PC release of the game (`sh3.exe` SHA-256 `8ff8f748…0918`). The package sets the extended memory budget; without it the game runs out of memory.
- **Controls file:** if the game has written `ux0:data/sh3-vita/key.ini`, its bindings replace the port's layout. Delete it to get the default layout back.
- **Resolution:** the game always renders at 960x544; the Options resolution row shows only that mode.
- **First launch:** the shaders are compiled on the console the first time each one is needed, so the first minutes stutter more than later runs (the compiled shaders are kept in `ux0:data/sh3-vita/shc-r1`).
- **Logs:** `ux0:data/sh3-vita/sh3log.txt`. Include it when reporting a problem.
- **Vita3K:** the emulator is useful for some checks but does not behave like the console here; results from it are not console results.
