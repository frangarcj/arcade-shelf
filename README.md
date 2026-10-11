# arcade-shelf

A 3D game selector for emulators. Each game is an object on a carousel:

- an **arcade cabinet**, with its marquee, its screen, a control panel with
  sticks and buttons, and a coin door;
- a **disc** for console games, with the cover printed on its label.

The game's versions are listed beside it: MAME sets, regions, revisions.
The selector runs on the PS Vita (vitaGL) and on macOS (OpenGL 2.1), from
the same code.

The application keeps the window, the GL context, the pad and the
emulator. It gives arcade-shelf a catalogue, calls one function per frame,
and launches what the user picks.

## Use

```c
#include <arcade_shelf.h>

static const ShelfVersion versions[] = {
	{ "soulclbr", "World, SOC12/VER.A2", "ux0:data/app/roms/soulclbr.zip", 1998 },
	{ "soulclbrac", "Asia, SOC14/VER.C", "ux0:data/app/roms/soulclbrac.zip", 1998 },
};
static const ShelfGame games[] = {
	{ "soulclbr", "Soul Calibur", "Namco System 12", SHELF_CABINET, versions, 2 },
};
static const ShelfSystemStyle styles[] = {
	/* system, body, trim, kick panel top/bottom, marquee gradient (0xAABBGGRR) */
	{ "Namco System 12", 0xffa54840, 0xff343ee8, 0xff6e302c, 0xff3c1816, 0xff5046f0, 0xffc83282 },
};

ShelfConfig config = { "My", "Emulator", "ux0:data/app", "Copy games to ux0:data/app/roms.",
	960, 544, styles, 1 };
shelf_init(&config);                 /* with the GL context current */
shelf_set_games(games, 1);
for (;;) {
	ShelfInput input = { buttons, right_stick_x, right_stick_y, dt };
	char launch[512];
	int action = shelf_frame(&input, launch, sizeof(launch));
	if (action == SHELF_LAUNCH) { /* run launch */ }
	if (action == SHELF_QUIT) break;
	/* swap buffers */
}
shelf_shutdown();
```

Controls:

| Input | Action |
| --- | --- |
| Left / right | Game |
| Up / down | Version |
| Right stick | Turn and tilt the selected object |
| Cross or Start | Play |
| Square | Options (when the application gives some) |
| Select | About: the application's credits (`ShelfConfig.about`) |
| R (or Cross with no games) | Rescan: `shelf_frame` returns `SHELF_RESCAN` |
| Triangle | Exit |

Play first plays an insert animation, after the Socket frontend: a disc
rises spinning faster and faster, stands upright and goes straight down
into a slot in the floor; for a cabinet, the camera flies into its screen,
as if entering the game. The screen then fades to black, and `shelf_frame` returns
`SHELF_LAUNCH`.

The last game and the version chosen for each game are kept in
`<data_root>/shelf.cfg`.

## Options and pause menu

`ShelfConfig.menus` gives the application's settings: a table of
`ShelfOption` (a heading, a label, a description, a key, and the values: a
list of choices, then optionally a range of numbers such as 30%-100%) and
`get_option` / `set_option` callbacks that read, apply and save them. The
selector shows them on Square.

The pause menu runs over a game: the application draws the game's last
frame, then calls `shelf_pause_frame` each frame, which draws over it and
leaves the GL state as it was.

```c
shelf_pause_open("Soul Calibur", slot, 0);
for (;;) {
	/* draw the game's last frame */
	int result = shelf_pause_frame(&input, &slot);
	if (result == SHELF_PAUSE_SAVE_STATE) { /* save to slot, then reopen: */
		shelf_pause_open("Soul Calibur", slot, 1); /* on the save-state page */
	}
	if (result == SHELF_PAUSE_RESUME) break;
	/* also RESTART, CLOSE, LOAD_STATE; swap buffers */
}
```

It lists Resume, Restart game, Save states (with `state_slots` and
`state_exists`), Options, the application's `pause_actions` (they return
`SHELF_PAUSE_ACTION` + their index; the menu stays open) and Close game.
Start + Select or Circle resumes. `shelf_toast` shows a short message for
two seconds over the shelf or the pause menu.

## Art

The art is optional and the user supplies it, named by the version's `art`
name, or its id when that is not set (MAME set names for arcade games). A
version without art uses its game's key:

| File | Used for |
| --- | --- |
| `snap/<id>.png` (or `titles/<id>.png`) | cabinet screen, stretched to 4:3 |
| `marquees/<id>.png` | cabinet marquee |
| `covers/<id>.png` | disc label, cropped round; PCSX AwO's covers fit as they are |

Without art, the marquee and the label show the title on the system's
colours, and the screen shows the id. The library ships no game art.

## Artwork download (optional)

With `ADB=1`, L opens a panel that downloads the missing snaps,
titles and marquees of the arcade games from
[Arcade Database](https://adb.arcadeitalia.net) by motoschifo, together
with each set's control data. It follows the service's published rules:

- one connection at a time;
- romsets queried in batches;
- the source credited on the panel;
- a User-Agent that names the application;
- only missing files, and only when the user asks.

Ask the site's author before you ship it. On the Vita, the application
must start the network (`sceNet`) and put a CA bundle in
`<data_root>/cacert.pem`.

## Build

```sh
git submodule update --init
make                          # macOS: build/libarcadeshelf.a, build/viewer
make vita                     # PS Vita: build-vita/libarcadeshelf.a
make vita VITAGL_DIR=../vitaGL  # with the application's vitaGL headers
make ADB=1 ...                # with the artwork download (libcurl)
```

The library builds its own Dear ImGui 1.61, which is the version that VitaSDK
ships. An application that uses arcade-shelf must not link another ImGui,
unless it builds the library with `make vita IMGUI=system`: then both use
VitaSDK's `libimgui`, and the library keeps its own ImGui context. It
links libpng, zlib and its GL: vitaGL on the Vita, `-framework OpenGL` on
macOS, where it runs on an OpenGL 2.1 context or on a 3.2+ core one (as
emulator renderers make; the viewer's `--gl-core`). For a Retina window, set
`framebuffer_width` / `framebuffer_height`: the 960 x 544 layout is scaled to
them.

## Viewer

`build/viewer` runs the selector on macOS with a catalogue from a text file
(`examples/viewer/demo_catalog.tsv`). It takes the keyboard (arrows; Z play;
A options; S or Esc exit; J L I K right stick) or a game controller.
`--pause` shows the pause menu instead, over a stand-in game frame.

It can also run headless with a fixed time step and save screenshots, to
check the UI without a display:

```sh
build/viewer --headless --script "R,R,D,SR,-" --step-frames 20 --shots /tmp/steps
```

## Credits

- Rinnegatamante: arcade-shelf grew from the 3D disc carousel of his
  PCSX AwO selector. The disc (its mesh and its shading: label sheen, black
  PS1 data face, silver face) and the ImGui renderer are adapted from that
  selector with his permission. arcade-shelf runs on his vitaGL, and PCSX
  AwO's PS1 covers fit the disc label as they are.
- Dear ImGui by Omar Cornut; Roboto by Christian Robertson; rjson from
  libretro-common.
- With `ADB=1`: Arcade Database by motoschifo, and ProgettoSnaps by AntoPisa.

## Licence

MIT, see `LICENSE`. Third-party parts are listed in `NOTICE`.
