# Known issues

State of the PS Vita build as of 14 November 2026 (version 0.0.1 plus the changes after it). "Console" means
seen on a real PS Vita; "PC" means the Linux test build only.

## Open

| Issue | Where | Notes |
|---|---|---|
| Dark rectangle over Heather's flashlight | Console | The lens flare. It is drawn with the right state and looks correct on the PC; on the Vita its additive blend appears to be lost, so the mostly black flare texture covers the scene. Under investigation. |
| Below 30 fps in heavier spots | Console | The 30 fps cap holds most of the time; some areas need ~35–47 ms of game code per frame and run at 17–20 fps. Loading spots drop briefly. |
| Movies at about 20 fps from the original `.000` files | Console | The game copies every frame into its 512x512 movie texture and the port re-uploads it each frame. Converting the movies to MP4 (README, Install) plays them with the hardware decoder at 30 fps (console: the opening movie). |
| MP4 movies: only the opening checked | Console | Not yet tested through the MP4 path: the end of a movie and the game moving on, skipping, the other six movies, a full-length A/V sync check, and fades/overlays, which are drawn under the video. The MP4 player keeps 32 MB of video memory aside; its effect in heavy areas is not measured. |
| Jagged edges | Console | No anti-aliasing: everything is drawn into off-screen buffers without multisampling. |
| Room sound effects play "dry" | PC | Footsteps, doors and items are audible again (PC), but the DirectSound effect the game asks for on them is not emulated. Not yet heard on the console. |
| Saving and loading a game | Console | Not tested yet. |
| Most of the game | Console | Only the opening and the first areas have been played. Other movies, endings, later areas and long sessions are unconfirmed. |

## Built but waiting for a console test

- In-game sound effects (footsteps, doors, items): fixed on the PC, where they were silent too.
- The Vita button layout described in the README (cross confirms, circle goes back and switches the flashlight, square runs, triangle opens the map, SELECT pauses).
- The credits page before the opening movie, and the title menu without Exit.

## Things to know

- **Requirements:** a Vita with custom firmware, `ur0:data/libshacccg.suprx`, and the 2003 PC release of the game (`sh3.exe` SHA-256 `8ff8f748…0918`). The package sets the extended memory budget; without it the game runs out of memory.
- **Controls file:** if the game has written `ux0:data/sh3/key.ini`, its bindings replace the port's layout. Delete it to get the default layout back.
- **Resolution:** the game always renders at 960x544; the Options resolution row shows only that mode.
- **First launch:** the shaders are compiled on the console the first time each one is needed, so the first minutes stutter more than later runs (the compiled shaders are kept in `ux0:data/sh3/shc-r1`).
- **Logs:** `ux0:data/sh3/sh4log.txt` (the name is inherited from the SH4 port). Include it when reporting a problem.
- **Vita3K:** the emulator is useful for some checks but does not behave like the console here; results from it are not console results.
