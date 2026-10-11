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
	const char *id;     /* art and settings name, e.g. MAME set "soulclbrac" */
	const char *label;  /* "Asia, SOC14/VER.C" (empty: id is shown) */
	const char *launch; /* returned on launch, e.g. a zip or image path */
	int year;
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

typedef struct {
	const char *title, *title_accent; /* header: "PCSX" "ReArcade" */
	const char *data_root;   /* art dirs, shelf.cfg */
	const char *empty_hint;  /* shown when the catalogue is empty */
	int width, height;       /* framebuffer size in pixels */
	const ShelfSystemStyle *styles; /* the first one is the default */
	int style_count;
} ShelfConfig;

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

/* Art, in data_root (MAME names for arcade; any id for consoles):
 *   snap/<id>.png or titles/<id>.png   screen of a cabinet
 *   marquees/<id>.png                  cabinet marquee
 *   covers/<id>.png                    disc label
 * Each falls back to the game's key. Missing art is drawn from the title. */

#ifdef __cplusplus
}
#endif

#endif
