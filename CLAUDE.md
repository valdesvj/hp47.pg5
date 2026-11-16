# HP47: working rules

- Work on a new branch; Victor creates and merges the PRs. Never force-push or rewrite history.
- Commit author: "Victor Valdes" <valdesvj@users.noreply.github.com>.
- The same base as ZX47, Chess 47, NAV47, TERM47 and GB47 (dmcp/, tools/pg5tool.py, test/sim): keep them in step.
- third_party/x48 is x48's Saturn CPU (GPL-2.0-or-later), changes marked "HP47" and listed in README.HP47.
- DMCP5: 8 KB of static RAM. Big buffers go on the heap (the RAM, the ROM, the page table, the LCD copy); check .bss.
- The ROM is HP's: never commit it (roms/ is ignored but for its README). The tests need roms/gxrom-r.
- make test, make keys and make shots before committing; look at every screen. The simulator's timings mean nothing.
- A key in src/keys.c changed: run make keys and look at its screen; then python3 tools/keymap.py docs/keymap.png.
- Ask before anything that can't be undone. Pull requests only for hp47.pg5.
