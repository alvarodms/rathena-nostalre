# nostalRE fork of rAthena

This fork carries the rAthena changes used by
[nostalRE](https://github.com/si-klyde/nostalre). Its
[launcher](https://github.com/si-klyde/nostalre-launcher) takes this branch as its
`server/` submodule and builds the Android app and the desktop servers from it
(`scripts/build-server.sh`, `scripts/build-apk.sh`). The app bundles
login, char and map servers built from this source, together with this tree's
`conf/`, `db/` and `npc/` folders and a SQLite database made from `sql-files/`.

rAthena is licensed under the GNU General Public License version 3
([LICENSE](LICENSE)). These changes are distributed under the same terms.

## Baseline and modifications

- Upstream: [rathena/rathena](https://github.com/rathena/rathena) at
  `d4b8e7b8f16061cc2496d377ac8f777ce72a4f39` (2026-10-06).
- Modified between 2026-10-08 and 2026-10-20 in the Ragnarok Solo integration
  project. The `nostalre` branch's first commit after the baseline holds every
  change; `git diff d4b8e7b8 -- <path>` shows any file's modifications.
- Most behavior changes carry a `Ragnarok Solo` comment.

Summary:

- Optional embedded SQLite backend instead of MySQL: `ENABLE_SQLITE` CMake
  option, `src/common/sql_sqlite.cpp`, the SQLite amalgamation in
  `3rdparty/sqlite` (public domain), `tools/mysql2sqlite.py`, and the
  `sql-compat-test` tool.
- Query fixes for SQLite operator precedence and trailing-space login IDs.
- `RA_KEEP_CWD` keeps the working directory when binaries live apart from
  `conf/`; overridable CMake output directories for cross builds.
- Monster HP bar for every packet version; packet obfuscation off.
- Hair style limits per sex and race; elemental ATK never negative.
- Single-player configuration: local-only IPs, instant character delete,
  pincode off, MVP EXP message on, homunculus growth message off.
- NPC and warp positions for current client map data (Renewal Izlude, no
  airship docks).
- Custom NPCs on (`npc/scripts_custom.conf`): Job Master, Warper, Healer
  (free), Stylist and Reset Girl, in Prontera's square. Prontera's Healer
  moved to 162,195, clear of the Ash-Vacuum Warper. `npc/nostalre_off.conf`
  (empty here) takes `delnpc:` lines for the ones the app's player turns off.
- Booster Event (`npc/custom/booster.txt`, off by default: the app adds an `npc:` line
  for it): kRO's 2021 Booster Promotion. A Booster Registration NPC (Prontera 168,300)
  gives each character a Booster Call and Booster Pack (1), and makes a Base Lv 1
  Novice a High Novice (transcended without a rebirth, like kRO's); the Booster Call
  (item 100337, its script was a TODO) summons the Booster Assistant next to the
  player (`duplicate_dynamic`), with the Booster Coin Shop, EXP potion exchange, daily
  hunting and item collection quests (reset at 4:00 server time). A Booster Equipment
  NPC (Prontera 166,300) trades tickets and coins for Metal, Booster, shadow and
  Illusion gear. The shops are barters in `npc/custom/booster_barters.yml`, imported by
  `npc/custom/barters.yml`, so they load even with the event off (nothing opens them).
- Job Master offers the 4th job change: its check kept the transcendent bit,
  which no 4th job's class has, so no transcendent 3rd job got the option.

## Population Engine

Fake players ("shells") from the
[Population Engine](https://github.com/YlenXWalker/Population-Engine) by Louis T
Steinhil (GPL-3.0), taken from its commit `2cfb45d` (built on the same rAthena
baseline): `src/map/population_engine*`, `db/population_*.yml`,
`conf/battle/population_engine.conf`, `npc/custom/population/arena.txt`, and the
engine's hooks in rAthena's own files. Left out: its committed `conf/import/`
and `db/import/` trees, the `PACKETVER` define in `src/custom/defines_pre.hpp`
(CMake sets it), the FluxCP `sql-files/population_engine/` table and the build
files we do not use. Our changes carry a `nostalRE` comment:

- `population_engine_enable` (off by default): when off, no yml is read, no
  timer runs, `@populate`/`@reloadpopenginedb` refuse and the arena NPC idles.
  `conf/battle_athena.conf` imports the engine conf before the app's import.
- The FluxCP stats query (MySQL-only syntax, a table we do not ship) is compiled
  out of the SQLite build.
- Shells can join a player's party as companions: `party_invite` on a shell, or a
  whispered "party" (which makes a party for a player without one); "leave" or
  "bye" dismisses one, and so does expelling it. A companion holds a member slot
  on the map server only (`party_companion_add`, `src/map/party.cpp`), so the
  char-server and the database never see it; one record in
  `population_engine.cpp` says whose it is, and every way out goes through
  `population_engine_companion_dismiss`. It follows its owner, across maps too,
  and helps in fights. It reacts to its owner's moves a moment late, trails a
  walking owner at its own distance, and around an owner standing still takes a
  free cell of its own by role (a tank in front, healers, casters and ranged
  weapons behind, the rest at a side), shifting now and then. It is a party member for EXP: Even Share pools its kills
  with the player's (a party made by the whisper starts on Even Share), Each Take
  keeps them its own, and its share is lost. The char-server's
  `party_share_level` gap applies to it too (`party_exp_share_allowed`, read from
  `inter_athena.conf` by the map server as well). Loot priority on its kills goes
  to its owner, like a mercenary's. It can die: Resurrection and the like revive
  it, and if nobody does it gets up next to its owner when they change map or
  after `population_engine_companion_revive_ms`. It leaves on logout, stays out
  of instances, and nothing brings it back after a restart.
  `population_engine_companion_max` sets the limit per player.
- `population_engine_max_count` 300.
- `population_engine_map_levels` (on): autosummoned shells take their level from
  the mean level of their map's permanent monsters, with a floor by class (a
  Knight is at least 45); towns and maps with no monsters use 30-85. A class
  whose floor is clearly above the map steps down (Knight to Swordsman to
  Novice); a first class on a map with monsters from level 40 may step up to a
  second class, and from level 60 always does (Swordsman to Knight or Crusader).
  Either way it still counts as its spawn-table job. Stats are fitted to what the
  level has in `db/re/statpoint.yml`, gear to what it can wear; a shell below its
  whole weapon pool gets the plainest weapon of that kind its job can wear.
  Off, and arena and `@populate` shells either way: the profile's range or 99/70.
  `@populate levels` lists the shells on your map.

## Building the Android servers

The app's servers are built for Renewal at packet version 20250716, with the
Android NDK (r30, `30.0.16248370`), CMake and Ninja:

```sh
NDK="$ANDROID_NDK_HOME"
cmake -S . -B build-android -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SQLITE=ON -DENABLE_WEB_SERVER=OFF -DPACKETVER=20250716 \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 \
  -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=BOTH \
  -DHAVE_TLS_EXITCODE=0 -DHAVE_MONOTONIC_CLOCK_EXITCODE=0 \
  -DRT_LIBRARY="$NDK/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/lib/aarch64-linux-android/26/libc.so" \
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$PWD/build-android/bin"
ninja -C build-android login-server char-server map-server
for exe in login-server char-server map-server; do
  "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip" "build-android/bin/$exe"
done
```

The app ships them renamed as `libloginserver.so`, `libcharserver.so` and
`libmapserver.so`.

Server data and database, as packed into the app:

```sh
mkdir -p stage
git ls-files -z conf db npc | tar --null -T - -cf - | tar -xf - -C stage
for t in conf/import-tmpl conf/msg_conf/import-tmpl db/import-tmpl; do
  [ -d "stage/${t%-tmpl}" ] || cp -r "stage/$t" "stage/${t%-tmpl}"
done
(cd stage && python3 -m zipfile -c ../server.zip conf db npc)
python3 tools/mysql2sqlite.py sql-files/main.sql sql-files/logs.sql \
  sql-files/web.sql sql-files/roulette_default_data.sql | sqlite3 -bail ragnarok.db
```

At runtime the app sets `RA_KEEP_CWD=1` and `RA_SQLITE_DIR` to the folder
holding `ragnarok.db`.

A host build uses the same CMake options without the toolchain arguments.

## Tags

- `nostalre-v0.1.0`: source of the servers in nostalRE v0.1.0.
- `nostalre-v0.1.1`: source of the servers in nostalRE v0.1.1 (drops go straight to the inventory).
