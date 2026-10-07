# Gathering patterns

Each Lua file defines one editable pattern. The app installs these defaults into
its writable pattern folder without overwriting existing files. Open that folder
from **Game → Patterns**, edit or add a file, then reload while stopped. Source is
captured for each launch, so edits do not change an active run.

`configure(settings)` receives `width`, `scale` (percent), `repetitions`,
`invert-fb`, `invert-lr`, and `facing` (0–7). Scripts choose how to use these values.
`configure` initializes movement and returns an empty table. It does not supply
speed or run limits. The Natro-derived patterns use scale/repetitions; Square, Lines and Cross also
use width. Facing is already applied by Gathering before execution.

`next(view)` returns one movement or key operation. `cycle_end` marks a safe point
for optional correction. The host controls real movement speed, input release,
backpack/time limits, interrupts, return and recovery. A script does not need to
implement them. The files use the same sandbox as custom Lua patterns.

The twelve Natro-derived files are attributed in `licenses/Natro-ports.txt`.
