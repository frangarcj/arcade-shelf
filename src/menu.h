/* The options panel and the pause menu: input and drawing. shelf.cpp runs
 * them inside its ImGui frames. */
#ifndef ARCADE_SHELF_MENU_H
#define ARCADE_SHELF_MENU_H

#include "arcade_shelf.h"

struct ImDrawList;

/* The application's settings (NULL: none) and the screen size. */
void menu_init(const ShelfMenus *menus, float width, float height);
bool menu_has_options();

void options_open();
bool options_is_open();
/* Takes the input while the panel is open; it closes itself. */
void options_input(unsigned buttons, float dt);
void options_draw(ImDrawList *draw);

void pause_open(const char *game, int slot, int save_states);
/* SHELF_PAUSE_*; *slot gets the slot selected. */
int pause_input(unsigned buttons, float dt, int *slot);
void pause_draw(ImDrawList *draw);

#endif
