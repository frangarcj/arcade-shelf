#include "menu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>

#include <imgui.h>

#include "theme.h"

using namespace theme;

namespace {

const ShelfMenus *menus;
const char *about_text;
float W, H;

/* ---- options ---- */

struct Options {
	bool open;
	int selected;
	float scroll; /* list offset in pixels, eases toward the selected row */
	Keys keys;
} options;

const float kHeadingH = 26.0f, kRowH = 32.0f;

int value_count(const ShelfOption &o)
{
	return o.choice_count + (o.range_max > o.range_min ? o.range_max - o.range_min + 1 : 0);
}

std::string value_at(const ShelfOption &o, int i)
{
	if (i < o.choice_count)
		return o.choices[i].value;
	return std::to_string(o.range_min + i - o.choice_count);
}

std::string label_at(const ShelfOption &o, int i)
{
	if (i < o.choice_count)
		return o.choices[i].label ? o.choices[i].label : o.choices[i].value;
	return value_at(o, i) + (o.range_suffix ? o.range_suffix : "");
}

const char *current_value(const ShelfOption &o)
{
	const char *value = menus->get_option ? menus->get_option(menus->user, o.key) : NULL;
	return value ? value : "";
}

/* The current value's index, or -1 if it is none of them. */
int current_index(const ShelfOption &o)
{
	const char *value = current_value(o);
	for (int i = 0; i < o.choice_count; i++)
		if (!strcmp(value, o.choices[i].value))
			return i;
	if (o.range_max > o.range_min && *value) {
		char *end;
		const long number = strtol(value, &end, 10);
		if (!*end && number >= o.range_min && number <= o.range_max)
			return o.choice_count + (int)(number - o.range_min);
	}
	return -1;
}

std::string current_label(const ShelfOption &o)
{
	const int index = current_index(o);
	return index >= 0 ? label_at(o, index) : current_value(o);
}

void step_option(int index, int direction)
{
	const ShelfOption &o = menus->options[index];
	const int count = value_count(o);
	if (count <= 0 || !menus->set_option)
		return;
	int current = current_index(o);
	if (current < 0)
		current = direction > 0 ? -1 : 0;
	const int next = ((current + direction) % count + count) % count;
	menus->set_option(menus->user, o.key, value_at(o, next).c_str());
}

bool new_category(int index)
{
	const char *category = menus->options[index].category;
	const char *previous = index > 0 ? menus->options[index - 1].category : NULL;
	return category && *category && (!previous || strcmp(category, previous));
}

/* Top of option index's row, from the top of the list. */
float row_top(int index)
{
	float y = 0.0f;
	for (int i = 0; i <= index; i++) {
		if (new_category(i))
			y += kHeadingH;
		if (i < index)
			y += kRowH;
	}
	return y;
}

/* A small filled arrow, pointing left (-1) or right (1). */
void arrow(ImDrawList *draw, float x, float y, int direction, ImU32 color)
{
	draw->AddTriangleFilled(ImVec2(x + 4.0f * direction, y), ImVec2(x - 3.0f * direction, y - 5),
		ImVec2(x - 3.0f * direction, y + 5), color);
}

/* ---- pause menu ---- */

enum { ITEM_RESUME, ITEM_RESTART, ITEM_STATES, ITEM_OPTIONS, ITEM_CLOSE };
enum { STATE_SAVE, STATE_LOAD, STATE_SLOT };

struct Pause {
	std::string game;
	bool states_page;
	int selected, state_selected;
	int slot;
	Keys keys;
} pause;

std::vector<int> pause_items()
{
	std::vector<int> items = { ITEM_RESUME, ITEM_RESTART };
	if (menus && menus->state_slots > 0)
		items.push_back(ITEM_STATES);
	if (menu_has_options())
		items.push_back(ITEM_OPTIONS);
	items.push_back(ITEM_CLOSE);
	return items;
}

const char *item_label(int item)
{
	switch (item) {
	case ITEM_RESUME: return "Resume";
	case ITEM_RESTART: return "Restart game";
	case ITEM_STATES: return "Save states";
	case ITEM_OPTIONS: return "Options";
	default: return "Close game";
	}
}

bool slot_used(int slot)
{
	return menus && menus->state_exists && menus->state_exists(menus->user, slot);
}

/* A dark rounded panel with an amber edge. */
void panel(ImDrawList *draw, ImVec2 a, ImVec2 b)
{
	draw->AddRectFilled(a, b, rgba(24, 29, 54), 12.0f);
	draw->AddRect(a, b, rgba(255, 180, 0, 0.35f), 12.0f);
}

/* A list row: the selected one gets the amber bar of the version list. */
void row(ImDrawList *draw, float x0, float x1, float y, float h, bool selected)
{
	if (!selected)
		return;
	draw->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + h), rgba(255, 180, 0, 0.16f), 6.0f);
	draw->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + 3, y + h), rgba(255, 180, 0), 2.0f);
}

/* ---- credits ---- */

struct About {
	bool open;
	float scroll, target; /* pixels */
	Keys keys;
} about;

/* Lays the text out from y; returns the height. draw NULL: measure only. */
float about_layout(ImDrawList *draw, float x, float y, float width)
{
	const float top = y;
	const char *line = about_text;
	while (line && *line) {
		const char *end = strchr(line, '\n');
		const std::string text(line, end ? end : line + strlen(line));
		line = end ? end + 1 : NULL;
		if (text.empty()) {
			y += 10.0f;
		} else if (text.compare(0, 2, "# ") == 0) {
			y += 6.0f;
			if (draw)
				draw->AddText(font_small(), 13.0f, ImVec2(x, y), rgba(255, 180, 0), text.c_str() + 2);
			y += 22.0f;
		} else {
			const ImVec2 size = font_small()->CalcTextSizeA(15.0f, FLT_MAX, width, text.c_str());
			if (draw)
				draw->AddText(font_small(), 15.0f, ImVec2(x, y), rgba(200, 204, 225), text.c_str(), NULL, width);
			y += size.y + 4.0f;
		}
	}
	return y - top;
}

} // namespace

void menu_init(const ShelfMenus *m, const char *credits, float width, float height)
{
	menus = m;
	about_text = credits && *credits ? credits : NULL;
	about = About();
	W = width;
	H = height;
	options = Options();
	pause = Pause();
}

bool menu_has_options()
{
	return menus && menus->option_count > 0;
}

void options_open()
{
	if (!menu_has_options())
		return;
	options.open = true;
	options.keys = Keys();
	/* What opened the panel is still held: wait for its release. */
	options.keys.previous = ~0u;
	options.scroll = -1.0f;
}

bool options_is_open()
{
	return options.open;
}

void options_input(unsigned buttons, float dt)
{
	Keys &k = options.keys;
	const int count = menus->option_count;
	if (k.pressed(buttons, SHELF_UP, dt))
		options.selected = (options.selected + count - 1) % count;
	if (k.pressed(buttons, SHELF_DOWN, dt))
		options.selected = (options.selected + 1) % count;
	if (k.pressed(buttons, SHELF_LEFT, dt))
		step_option(options.selected, -1);
	if (k.pressed(buttons, SHELF_RIGHT, dt) || k.pressed(buttons, SHELF_CROSS, dt))
		step_option(options.selected, 1);
	if (k.pressed(buttons, SHELF_CIRCLE, dt) || k.pressed(buttons, SHELF_SQUARE, dt) ||
		k.pressed(buttons, SHELF_TRIANGLE, dt))
		options.open = false;
	k.end_frame(buttons);
}

void options_draw(ImDrawList *draw)
{
	const ImVec2 p0(48, 26), p1(W - 48, H - 26);
	const float x0 = p0.x + 28, x1 = p1.x - 28;
	const float list_top = p0.y + 70, list_bottom = p1.y - 118;
	const ShelfOption &chosen = menus->options[options.selected];

	draw->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), rgba(5, 7, 16, 0.72f));
	panel(draw, p0, p1);
	draw->AddText(font_title(), 26.0f, ImVec2(x0, p0.y + 20), rgba(245, 245, 250), "Options");

	/* Keep the selected row in view, easing toward it. */
	const float top = row_top(options.selected), visible = list_bottom - list_top;
	const float total = row_top(menus->option_count - 1) + kRowH;
	float target = options.scroll < 0.0f ? 0.0f : options.scroll;
	if (top - (new_category(options.selected) ? kHeadingH : 0.0f) < target)
		target = top - (new_category(options.selected) ? kHeadingH : 0.0f);
	if (top + kRowH > target + visible)
		target = top + kRowH - visible;
	target = std::max(0.0f, std::min(target, std::max(0.0f, total - visible)));
	options.scroll = options.scroll < 0.0f ? target : options.scroll + (target - options.scroll) * 0.3f;

	draw->PushClipRect(ImVec2(p0.x, list_top), ImVec2(p1.x, list_bottom), true);
	float y = list_top - options.scroll;
	for (int i = 0; i < menus->option_count; i++) {
		const ShelfOption &o = menus->options[i];
		if (new_category(i)) {
			draw->AddText(font_small(), 13.0f, ImVec2(x0, y + 9), rgba(255, 180, 0), o.category);
			y += kHeadingH;
		}
		const bool selected = i == options.selected;
		row(draw, x0 - 8, x1 + 8, y, kRowH - 2, selected);
		draw->AddText(font_body(), 17.0f, ImVec2(x0 + 8, y + 6), selected ? rgba(255, 255, 255) : rgba(200, 204, 225),
			o.label);
		const std::string value = current_label(o);
		const ImVec2 size = font_body()->CalcTextSizeA(17.0f, FLT_MAX, 0.0f, value.c_str());
		const ImU32 value_color = selected ? rgba(255, 200, 90) : rgba(160, 165, 195);
		draw->AddText(font_body(), 17.0f, ImVec2(x1 - 20 - size.x, y + 6), value_color, value.c_str());
		if (selected) {
			arrow(draw, x1 - 30 - size.x, y + 15, -1, value_color);
			arrow(draw, x1 - 6, y + 15, 1, value_color);
		}
		y += kRowH;
	}
	draw->PopClipRect();
	draw->AddLine(ImVec2(x0, list_bottom + 8), ImVec2(x1, list_bottom + 8), rgba(255, 255, 255, 0.08f));
	if (chosen.description)
		draw->AddText(font_small(), 15.0f, ImVec2(x0, list_bottom + 18), rgba(170, 175, 200), chosen.description,
			NULL, x1 - x0);
	const float fy = p1.y - 24;
	float x = x0;
	x = text_hint(draw, x, fy, "^ v", "Option");
	x = text_hint(draw, x, fy, "< >", "Change");
	hint(draw, x, fy, SHELF_CIRCLE, "Back");
}

bool menu_has_about()
{
	return about_text != NULL;
}

void about_open()
{
	about.open = about_text != NULL;
	about.scroll = about.target = 0.0f;
	about.keys = Keys();
	about.keys.previous = ~0u;
}

bool about_is_open()
{
	return about.open;
}

void about_input(unsigned buttons, float dt)
{
	Keys &k = about.keys;
	if (k.pressed(buttons, SHELF_UP, dt))
		about.target -= 60.0f;
	if (k.pressed(buttons, SHELF_DOWN, dt))
		about.target += 60.0f;
	if (k.pressed(buttons, SHELF_CIRCLE, dt) || k.pressed(buttons, SHELF_SELECT, dt) ||
		k.pressed(buttons, SHELF_TRIANGLE, dt) || k.pressed(buttons, SHELF_CROSS, dt))
		about.open = false;
	k.end_frame(buttons);
}

void about_draw(ImDrawList *draw)
{
	const ImVec2 p0(48, 26), p1(W - 48, H - 26);
	const float x0 = p0.x + 28, x1 = p1.x - 28;
	const float top = p0.y + 70, bottom = p1.y - 52;
	const float height = about_layout(NULL, x0, 0.0f, x1 - x0);
	const float most = std::max(0.0f, height - (bottom - top));
	about.target = std::max(0.0f, std::min(about.target, most));
	about.scroll += (about.target - about.scroll) * 0.3f;

	draw->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), rgba(5, 7, 16, 0.72f));
	panel(draw, p0, p1);
	draw->AddText(font_title(), 26.0f, ImVec2(x0, p0.y + 20), rgba(245, 245, 250), "About");
	draw->PushClipRect(ImVec2(p0.x, top), ImVec2(p1.x, bottom), true);
	about_layout(draw, x0, top - about.scroll, x1 - x0);
	draw->PopClipRect();
	const float fy = p1.y - 24;
	float x = x0;
	if (most > 0.0f)
		x = text_hint(draw, x, fy, "^ v", "Scroll");
	hint(draw, x, fy, SHELF_CIRCLE, "Back");
}

void pause_open(const char *game, int slot, int save_states)
{
	pause.game = game ? game : "";
	pause.states_page = save_states && menus && menus->state_slots > 0;
	pause.selected = 0;
	pause.state_selected = STATE_SAVE;
	pause.slot = menus && menus->state_slots > 0 ? std::max(0, std::min(slot, menus->state_slots - 1)) : 0;
	pause.keys = Keys();
	pause.keys.previous = ~0u;
	options.open = false;
}

int pause_input(unsigned buttons, float dt, int *slot)
{
	Keys &k = pause.keys;
	int result = SHELF_PAUSE_NONE;
	const unsigned combo = SHELF_START | SHELF_SELECT;
	*slot = pause.slot;
	if ((buttons & combo) == combo && (k.previous & combo) != combo) {
		k.end_frame(buttons);
		return SHELF_PAUSE_RESUME;
	}
	if (options.open) {
		options_input(buttons, dt);
		k.end_frame(buttons);
		return SHELF_PAUSE_NONE;
	}
	if (pause.states_page) {
		const int slots = menus->state_slots;
		if (k.pressed(buttons, SHELF_UP, dt))
			pause.state_selected = (pause.state_selected + 2) % 3;
		if (k.pressed(buttons, SHELF_DOWN, dt))
			pause.state_selected = (pause.state_selected + 1) % 3;
		if (k.pressed(buttons, SHELF_L, dt) || (pause.state_selected == STATE_SLOT && k.pressed(buttons, SHELF_LEFT, dt)))
			pause.slot = (pause.slot + slots - 1) % slots;
		if (k.pressed(buttons, SHELF_R, dt) || (pause.state_selected == STATE_SLOT && k.pressed(buttons, SHELF_RIGHT, dt)))
			pause.slot = (pause.slot + 1) % slots;
		if (k.pressed(buttons, SHELF_CROSS, dt)) {
			if (pause.state_selected == STATE_SAVE)
				result = SHELF_PAUSE_SAVE_STATE;
			else if (pause.state_selected == STATE_LOAD && slot_used(pause.slot))
				result = SHELF_PAUSE_LOAD_STATE;
			else if (pause.state_selected == STATE_SLOT)
				pause.slot = (pause.slot + 1) % slots;
		}
		if (k.pressed(buttons, SHELF_CIRCLE, dt) || k.pressed(buttons, SHELF_SQUARE, dt) ||
			k.pressed(buttons, SHELF_TRIANGLE, dt))
			pause.states_page = false;
	} else {
		const std::vector<int> items = pause_items();
		const int count = (int)items.size();
		if (k.pressed(buttons, SHELF_UP, dt))
			pause.selected = (pause.selected + count - 1) % count;
		if (k.pressed(buttons, SHELF_DOWN, dt))
			pause.selected = (pause.selected + 1) % count;
		if (k.pressed(buttons, SHELF_CIRCLE, dt))
			result = SHELF_PAUSE_RESUME;
		if (k.pressed(buttons, SHELF_CROSS, dt)) {
			switch (items[pause.selected]) {
			case ITEM_RESUME: result = SHELF_PAUSE_RESUME; break;
			case ITEM_RESTART: result = SHELF_PAUSE_RESTART; break;
			case ITEM_STATES:
				pause.states_page = true;
				pause.state_selected = STATE_SAVE;
				break;
			case ITEM_OPTIONS: options_open(); break;
			default: result = SHELF_PAUSE_CLOSE; break;
			}
		}
	}
	*slot = pause.slot;
	k.end_frame(buttons);
	return result;
}

void pause_draw(ImDrawList *draw)
{
	draw->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), rgba(5, 7, 16, 0.6f));
	if (options.open) {
		options_draw(draw);
		return;
	}
	const float pw = 440.0f, rh = 44.0f;
	const int rows = pause.states_page ? 3 : (int)pause_items().size();
	const float ph = 112.0f + rows * (rh + 6) + 56.0f;
	const ImVec2 p0((W - pw) * 0.5f, floorf((H - ph) * 0.5f)), p1(p0.x + pw, p0.y + ph);
	const float x0 = p0.x + 28, x1 = p1.x - 28;
	panel(draw, p0, p1);
	draw->AddText(font_title(), 26.0f, ImVec2(x0, p0.y + 20), rgba(245, 245, 250),
		pause.states_page ? "Save states" : "Paused");
	draw->AddText(font_body(), 17.0f, ImVec2(x0, p0.y + 56), rgba(160, 165, 195), pause.game.c_str(), NULL);
	draw->AddLine(ImVec2(x0, p0.y + 90), ImVec2(x1, p0.y + 90), rgba(255, 255, 255, 0.08f));

	float y = p0.y + 104;
	if (pause.states_page) {
		const bool used = slot_used(pause.slot);
		static const char *labels[3] = { "Save state", "Load state", "Slot" };
		for (int i = 0; i < 3; i++, y += rh + 6) {
			const bool selected = i == pause.state_selected;
			const bool enabled = i != STATE_LOAD || used;
			row(draw, x0 - 8, x1 + 8, y, rh, selected);
			draw->AddText(font_body(), 18.0f, ImVec2(x0 + 8, y + 12),
				!enabled ? rgba(110, 114, 140) : selected ? rgba(255, 255, 255) : rgba(200, 204, 225), labels[i]);
			if (i != STATE_SLOT)
				continue;
			char text[16];
			snprintf(text, sizeof(text), "%d", pause.slot);
			const ImU32 color = selected ? rgba(255, 200, 90) : rgba(200, 204, 225);
			const char *status = used ? "Saved" : "Empty";
			const ImVec2 status_size = font_small()->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, status);
			const float chip_x = x1 - status_size.x - 16;
			draw->AddRectFilled(ImVec2(chip_x, y + 12), ImVec2(x1, y + 32),
				used ? rgba(100, 220, 160, 0.18f) : rgba(255, 255, 255, 0.06f), 10.0f);
			draw->AddText(font_small(), 13.0f, ImVec2(chip_x + 8, y + 22 - status_size.y * 0.5f),
				used ? rgba(130, 230, 180) : rgba(150, 155, 185), status);
			const ImVec2 size = font_body()->CalcTextSizeA(18.0f, FLT_MAX, 0.0f, text);
			const float nx = chip_x - 30 - size.x;
			draw->AddText(font_body(), 18.0f, ImVec2(nx, y + 12), color, text);
			if (selected) {
				arrow(draw, nx - 12, y + 22, -1, color);
				arrow(draw, nx + size.x + 12, y + 22, 1, color);
			}
		}
	} else {
		const std::vector<int> items = pause_items();
		for (int i = 0; i < (int)items.size(); i++, y += rh + 6) {
			const bool selected = i == pause.selected;
			row(draw, x0 - 8, x1 + 8, y, rh, selected);
			draw->AddText(font_body(), 18.0f, ImVec2(x0 + 8, y + 12), selected ? rgba(255, 255, 255) : rgba(200, 204, 225),
				item_label(items[i]));
		}
	}
	const float fy = p1.y - 26;
	float x = x0;
	x = hint(draw, x, fy, SHELF_CROSS, "Select");
	if (pause.states_page) {
		x = text_hint(draw, x, fy, "< >", "Slot");
		hint(draw, x, fy, SHELF_CIRCLE, "Back");
	} else {
		x = hint(draw, x, fy, SHELF_CIRCLE, "Resume");
	}
}
