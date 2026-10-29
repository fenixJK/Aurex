# Gathering patterns

Each Lua file defines one editable pattern. The app installs these defaults into
its writable pattern folder, updating unedited defaults while preserving custom
files. Open that folder
from **Game → Patterns**, edit or add a file, then reload while stopped. Source is
captured for each launch, so edits do not change an active run.

`configure(settings)` receives `width`, `scale` (percent), `repetitions`,
`invert-fb`, `invert-lr`, and `facing` (0–7). Scripts choose how to use these values.
`configure` initializes movement and returns an empty table. It does not supply
speed or run limits. The Natro-derived patterns use scale/repetitions; Square, Lines and Cross also
use width. Facing is already applied by Gathering before execution.

`next(view)` returns one movement or key operation. `camera` operations accept
`rotate-left`, `rotate-right`, `pitch-up` or `pitch-down` and a `steps` count.
Balance pitch changes within a cycle; the host also restores outstanding pitch
when a timed, bag, objective or priority interrupt ends gathering. `cycle_end` marks a safe point
for optional correction. The host controls real movement speed, input release,
backpack/time limits, interrupts, return and recovery. A script does not need to
implement them. The files use the same sandbox as custom Lua patterns.

The twelve Natro-derived files are attributed in `licenses/Natro-ports.txt`.

Kettle is a conversion of Dully176’s supplied AHK pattern (version 0.1.0). It keeps
the two-pass Pine route, wall offsets, diagonal moves and camera turns, with the
original digital-token stops disabled. Use 100% scale and 1 repetition for the
original geometry. Its recommended field settings are listed in `Kettle.lua`;
selecting a pattern does not overwrite your field settings.
