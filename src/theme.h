/* What the selector and the menus share: fonts, colours, button hints and
 * d-pad repeat. Valid between shelf_init and shelf_shutdown. */
#ifndef ARCADE_SHELF_THEME_H
#define ARCADE_SHELF_THEME_H

#include <imgui.h>

namespace theme {

ImFont *font_small(); /* 15 px */
ImFont *font_body();  /* 18 px */
ImFont *font_title(); /* 30 px */

ImU32 rgba(int r, int g, int b, float a = 1.0f);
/* Text centred in [x0, x1] at y, shrunk to fit. */
void centred_text(ImDrawList *draw, ImFont *font, float size, float x0, float x1, float y, ImU32 color,
	const char *text);
/* A PlayStation button glyph (SHELF_CROSS...) and its caption; returns the next x. */
float hint(ImDrawList *draw, float x, float y, unsigned button, const char *caption);
/* A key cap with text ("< >", "SELECT") and its caption; returns the next x. */
float text_hint(ImDrawList *draw, float x, float y, const char *key, const char *caption);

/* Presses since the last frame; a held d-pad direction repeats. */
struct Keys {
	unsigned previous = 0, repeat_button = 0;
	float repeat_time = 0.0f;

	bool pressed(unsigned buttons, unsigned button, float dt);
	/* Call once a frame, after the pressed() calls. */
	void end_frame(unsigned buttons) { previous = buttons; }
};

}

#endif
