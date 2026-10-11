/* arcade-shelf: a 3D game selector for emulators (MIT licence).
 *
 * Each game is an object on a carousel: an arcade cabinet, or a disc for
 * consoles. Its versions (MAME sets, regions, revisions) are listed beside
 * it. The application gives the catalogue and owns the window, the GL
 * context and the pad; it calls shelf_frame once per frame and launches
 * what the user picks. Runs on vitaGL (PS Vita) and on OpenGL 2.1 (macOS).
 */
#ifndef ARCADE_SHELF_H
#define ARCADE_SHELF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Buttons, PlayStation layout. */
enum {
	SHELF_UP = 1 << 0, SHELF_DOWN = 1 << 1, SHELF_LEFT = 1 << 2, SHELF_RIGHT = 1 << 3,
	SHELF_CROSS = 1 << 4, SHELF_CIRCLE = 1 << 5, SHELF_TRIANGLE = 1 << 6, SHELF_SQUARE = 1 << 7,
	SHELF_L = 1 << 8, SHELF_R = 1 << 9, SHELF_START = 1 << 10, SHELF_SELECT = 1 << 11
};

typedef struct {
	unsigned buttons;       /* SHELF_* held now */
	float stick_x, stick_y; /* right stick, -1..1: turns the selected object */
	float dt;               /* seconds since the last frame */
} ShelfInput;

/* How a game is shown. */
typedef enum {
	SHELF_CABINET, /* upright arcade cabinet: marquee, screen, control panel */
	SHELF_DISC     /* optical disc with its cover printed on the label */
} ShelfItemKind;

/* One version of a game: what is launched. */
typedef struct {
	const char *id;     /* settings name, e.g. MAME set "soulclbrac" */
	const char *label;  /* "Asia, SOC14/VER.C" (empty: id is shown) */
	const char *launch; /* returned on launch, e.g. a zip or image path */
	int year;
	const char *art;    /* art file name without ".png"; NULL: id */
} ShelfVersion;

/* A game: one object on the carousel. */
typedef struct {
	const char *key;    /* art fallback name, e.g. MAME parent "soulclbr" */
	const char *title;  /* "Soul Calibur" */
	const char *system; /* "Namco System 12"; picks a ShelfSystemStyle */
	ShelfItemKind kind;
	const ShelfVersion *versions;
	int version_count;
} ShelfGame;

/* Colours of one system's objects, IM_COL32 order (0xAABBGGRR). */
typedef struct {
	const char *system;
	uint32_t body, trim;            /* cabinet sides and T-molding; disc ring */
	uint32_t kick_top, kick_bottom; /* cabinet kick panel gradient */
	uint32_t marquee_a, marquee_b;  /* generated marquee / label gradient */
} ShelfSystemStyle;

/* ---- options and save states, owned by the application ----
 * Settings are strings by key. The shelf lists them (Square in the
 * selector, Options in the pause menu) and changes them with set_option,
 * which applies and saves them. */
typedef struct {
	const char *value; /* stored: "16:9_stretched" */
	const char *label; /* shown: "16:9 Stretched" */
} ShelfChoice;

typedef struct {
	const char *category;    /* heading, shown where it changes: "VIDEO" */
	const char *label;       /* "Aspect Ratio" */
	const char *description; /* shown under the list for the selected one */
	const char *key;
	const ShelfChoice *choices;
	int choice_count;
	/* After the choices, the whole numbers range_min..range_max, if
	 * range_max > range_min, shown with range_suffix ("30%".."100%"). */
	int range_min, range_max;
	const char *range_suffix;
} ShelfOption;

typedef struct {
	const ShelfOption *options;
	int option_count;
	void *user;
	const char *(*get_option)(void *user, const char *key); /* NULL: unknown */
	void (*set_option)(void *user, const char *key, const char *value);
	/* Save-state slots 0..state_slots-1 in the pause menu; 0: none. */
	int state_slots;
	int (*state_exists)(void *user, int slot);
} ShelfMenus;

typedef struct {
	const char *title, *title_accent; /* header: "PCSX" "ReArcade" */
	const char *data_root;   /* art dirs, shelf.cfg */
	const char *empty_hint;  /* shown when the catalogue is empty */
	int width, height;       /* layout size: framebuffer pixels, unless set below */
	const ShelfSystemStyle *styles; /* the first one is the default */
	int style_count;
	const ShelfMenus *menus; /* not copied: kept until shelf_shutdown; NULL: none */
	/* Credits, shown on Select: lines of text; "# " starts a heading and an
	 * empty line a gap. Not copied. NULL: none. */
	const char *about;
	/* Framebuffer size in pixels when it is not width x height (a Retina
	 * window): the layout is scaled to it. 0: width x height. */
	int framebuffer_width, framebuffer_height;
} ShelfConfig;

/* shelf_frame: Cross launches (after its animation), Triangle quits,
 * Square opens the options, Select the credits. */
enum { SHELF_NONE, SHELF_LAUNCH, SHELF_QUIT };

/* Call with the GL context current. The configuration is copied. Returns
 * 0 if the GL resources cannot be made. */
int shelf_init(const ShelfConfig *config);
void shelf_shutdown(void);
/* Replaces the catalogue (copied). Games keep the order given. The last
 * game and version chosen are restored from shelf.cfg. */
void shelf_set_games(const ShelfGame *games, int count);
/* Draws one frame into the bound framebuffer. On SHELF_LAUNCH, launch
 * holds the chosen version's launch string. */
int shelf_frame(const ShelfInput *input, char *launch, size_t launch_size);

/* The pause menu, over a game: resume, restart, save states, options,
 * close. The application draws the game's last frame, then calls
 * shelf_pause_frame, which draws over it and keeps the GL state. Start +
 * Select or Circle resumes. */
enum {
	SHELF_PAUSE_NONE, SHELF_PAUSE_RESUME, SHELF_PAUSE_RESTART, SHELF_PAUSE_CLOSE,
	SHELF_PAUSE_SAVE_STATE, SHELF_PAUSE_LOAD_STATE /* in the slot returned */
};
/* game: the title in the header. slot: the save-state slot shown first.
 * save_states: open on the save-state page (after saving, say). */
void shelf_pause_open(const char *game, int slot, int save_states);
/* *slot: the slot selected now, for any result. */
int shelf_pause_frame(const ShelfInput *input, int *slot);

/* Art, in data_root, by the version's art name (its id unless art is set;
 * MAME set names for arcade):
 *   snap/<art>.png or titles/<art>.png   screen of a cabinet
 *   marquees/<art>.png                   cabinet marquee
 *   covers/<art>.png                     disc label (a square scan of the
 *                                        label, cropped round; PCSX AwO's
 *                                        covers fit as they are)
 * Each falls back to the game's key. Missing art is drawn from the title. */

#ifdef __cplusplus
}
#endif

#endif
