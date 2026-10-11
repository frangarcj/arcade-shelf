#include "arcade_shelf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>

#include <imgui.h>

#include "gl.h"
#include "cabinet3d.h"
#include "disc3d.h"
#ifdef ARCADE_SHELF_ADB
#include <sys/stat.h>
#include "adb_scraper.h"
#endif
#include "roboto_medium.inc" /* Roboto_Medium_ttf, from deps/imgui/misc/fonts */

namespace {

/* ---- catalogue ---- */

struct Version {
	std::string id, label, launch;
	int year;
	std::string art; /* art file name */
};

struct Game {
	std::string key, title, system;
	ShelfItemKind kind;
	std::vector<Version> versions;
	int selected; /* version shown */
};

/* Art and generated surfaces of one version, cached. */
struct Art {
	std::string id;
	UiTexture snap, marquee, cover;
	UiTexture marquee3d, screen3d, label3d;
	unsigned last_used;
};

struct State {
	std::string title, title_accent, data_root, empty_hint;
	int width, height;
	std::vector<ShelfSystemStyle> styles;
	std::vector<std::string> style_names; /* owns styles[i].system */
	std::vector<Game> games;
	int game;          /* selected game */
	float scroll;      /* carousel position, eases toward game */
	unsigned previous; /* buttons last frame */
	float repeat_time;
	unsigned repeat_button;
	std::vector<Art> art;
	std::vector<std::pair<std::string, unsigned> > kick; /* kick panel per system */
	unsigned frame;
	float time;       /* seconds, for the idle animation */
	float yaw, pitch; /* right stick turn of the selected object */
	int posed_game;   /* the game yaw/pitch belong to */
	float launch_time;  /* seconds into the launch animation; < 0 none */
	std::string launch; /* what is launched when that animation ends */
	bool adb_open;    /* the artwork download panel is shown */
	ImFont *font_small, *font_body, *font_title;
};

State *s;

/* Used when the application gives no style for a system. */
const ShelfSystemStyle kDefaultStyle = {
	"", IM_COL32(64, 72, 165, 255), IM_COL32(232, 62, 52, 255), IM_COL32(44, 48, 110, 255),
	IM_COL32(22, 24, 60, 255), IM_COL32(240, 70, 80, 255), IM_COL32(130, 50, 200, 255)
};

const ShelfSystemStyle &style_for(const std::string &system)
{
	for (const ShelfSystemStyle &style : s->styles)
		if (system == style.system)
			return style;
	return s->styles.empty() ? kDefaultStyle : s->styles[0];
}

std::string config_path()
{
	return s->data_root + "/shelf.cfg";
}

/* shelf.cfg: "game=<key>" for the last game, "<key>=<id>" per game. */
void load_config()
{
	FILE *f = fopen(config_path().c_str(), "r");
	char line[512];
	std::string last_game;
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char *eq = strchr(line, '=');
		line[strcspn(line, "\r\n")] = 0;
		if (!eq)
			continue;
		*eq = 0;
		if (!strcmp(line, "game")) {
			last_game = eq + 1;
			continue;
		}
		for (Game &game : s->games)
			if (game.key == line)
				for (size_t v = 0; v < game.versions.size(); v++)
					if (game.versions[v].id == eq + 1)
						game.selected = (int)v;
	}
	fclose(f);
	for (size_t i = 0; i < s->games.size(); i++)
		if (s->games[i].key == last_game)
			s->game = (int)i;
}

void save_config()
{
	FILE *f = fopen(config_path().c_str(), "w");
	if (!f)
		return;
	if (!s->games.empty())
		fprintf(f, "game=%s\n", s->games[s->game].key.c_str());
	for (const Game &game : s->games)
		fprintf(f, "%s=%s\n", game.key.c_str(), game.versions[game.selected].id.c_str());
	fclose(f);
}

/* ---- art: <dir>/<id>.png, else the game key's ---- */

bool load_art_file(const char *dir, const std::string &id, const std::string &key, UiTexture *texture)
{
	const std::string names[2] = { id, key };
	for (const std::string &name : names) {
		if (name.empty())
			continue;
		const std::string path = s->data_root + "/" + dir + "/" + name + ".png";
		if (ui_gl_load_png(path.c_str(), texture))
			return true;
	}
	return false;
}

void free_one(Art &art)
{
	UiTexture *textures[] = { &art.snap, &art.marquee, &art.cover, &art.marquee3d, &art.screen3d, &art.label3d };
	for (UiTexture *texture : textures)
		ui_gl_free_texture(texture);
}

void free_art()
{
	for (Art &art : s->art)
		free_one(art);
	s->art.clear();
}

Art *art_for(const Game &game)
{
	const std::string &id = game.versions[game.selected].art;
	for (Art &art : s->art)
		if (art.id == id) {
			art.last_used = s->frame;
			return &art;
		}
	if (s->art.size() >= 12) {
		auto oldest = std::min_element(s->art.begin(), s->art.end(),
			[](const Art &a, const Art &b) { return a.last_used < b.last_used; });
		free_one(*oldest);
		s->art.erase(oldest);
	}
	Art art{};
	art.id = id;
	art.last_used = s->frame;
	if (game.kind == SHELF_DISC) {
		load_art_file("covers", id, game.key, &art.cover);
	} else {
		if (!load_art_file("snap", id, game.key, &art.snap))
			load_art_file("titles", id, game.key, &art.snap);
		load_art_file("marquees", id, game.key, &art.marquee);
	}
	s->art.push_back(art);
	return &s->art.back();
}

/* ---- drawing helpers ---- */

ImU32 rgba(int r, int g, int b, float a = 1.0f)
{
	return IM_COL32(r, g, b, (int)(a * 255.0f));
}

/* Moves a colour toward the background: light 1 keeps it, 0 is the background. */
ImU32 fade(ImU32 color, float light)
{
	static const int background[3] = { 15, 18, 36 };
	int channel[3];
	for (int i = 0; i < 3; i++) {
		const int value = (color >> (8 * i)) & 0xff;
		channel[i] = background[i] + (int)((value - background[i]) * light);
	}
	return IM_COL32(channel[0], channel[1], channel[2], (color >> IM_COL32_A_SHIFT) & 0xff);
}

/* Draws text centred in [x0, x1] at y, shrunk to fit. */
void centred_text(ImDrawList *draw, ImFont *font, float size, float x0, float x1, float y, ImU32 color, const char *text)
{
	ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
	if (extent.x > x1 - x0) {
		size *= (x1 - x0) / extent.x;
		extent = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
	}
	draw->AddText(font, size, ImVec2(floorf((x0 + x1 - extent.x) * 0.5f), floorf(y - extent.y * 0.5f)), color, text);
}

/* Draws a texture inside a box, keeping its proportions (fit) or filling
 * the box and cropping (cover). */
void image_box(ImDrawList *draw, const UiTexture &texture, ImVec2 a, ImVec2 b, ImU32 tint, bool cover)
{
	const float box_w = b.x - a.x, box_h = b.y - a.y;
	const float sx = box_w / texture.width, sy = box_h / texture.height;
	const ImTextureID id = (ImTextureID)(uintptr_t)texture.id;
	if (!cover) {
		const float scale = std::min(sx, sy), w = texture.width * scale, h = texture.height * scale;
		const ImVec2 p(a.x + (box_w - w) * 0.5f, a.y + (box_h - h) * 0.5f);
		draw->AddImage(id, p, ImVec2(p.x + w, p.y + h), ImVec2(0, 0), ImVec2(1, 1), tint);
		return;
	}
	const float scale = std::max(sx, sy);
	const float u = (box_w / scale) / texture.width, v = (box_h / scale) / texture.height;
	draw->AddImage(id, a, b, ImVec2(0.5f - u * 0.5f, 0.5f - v * 0.5f), ImVec2(0.5f + u * 0.5f, 0.5f + v * 0.5f), tint);
}

/* A PlayStation face button glyph, then its caption; returns the next x. */
float hint(ImDrawList *draw, float x, float y, unsigned button, const char *caption)
{
	const ImVec2 c(x + 9, y);
	draw->AddCircleFilled(c, 9.0f, rgba(40, 44, 64), 20);
	switch (button) {
	case SHELF_CROSS:
		draw->AddLine(ImVec2(c.x - 4, c.y - 4), ImVec2(c.x + 4, c.y + 4), rgba(120, 170, 255), 2.0f);
		draw->AddLine(ImVec2(c.x - 4, c.y + 4), ImVec2(c.x + 4, c.y - 4), rgba(120, 170, 255), 2.0f);
		break;
	case SHELF_CIRCLE:
		draw->AddCircle(c, 4.5f, rgba(255, 110, 110), 16, 2.0f);
		break;
	case SHELF_TRIANGLE: {
		const ImVec2 t[3] = { ImVec2(c.x, c.y - 5), ImVec2(c.x + 5, c.y + 4), ImVec2(c.x - 5, c.y + 4) };
		draw->AddPolyline(t, 3, rgba(100, 220, 160), true, 2.0f);
		break;
	}
	default:
		break;
	}
	const ImVec2 extent = s->font_small->CalcTextSizeA(15.0f, FLT_MAX, 0.0f, caption);
	draw->AddText(s->font_small, 15.0f, ImVec2(x + 24, y - extent.y * 0.5f), rgba(235, 235, 245), caption);
	return x + 24 + extent.x + 22;
}

float text_hint(ImDrawList *draw, float x, float y, const char *key, const char *caption)
{
	const ImVec2 key_size = s->font_small->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, key);
	draw->AddRectFilled(ImVec2(x, y - 9), ImVec2(x + key_size.x + 12, y + 9), rgba(40, 44, 64), 4.0f);
	draw->AddText(s->font_small, 13.0f, ImVec2(x + 6, y - key_size.y * 0.5f), rgba(200, 205, 230), key);
	const ImVec2 extent = s->font_small->CalcTextSizeA(15.0f, FLT_MAX, 0.0f, caption);
	draw->AddText(s->font_small, 15.0f, ImVec2(x + key_size.x + 18, y - extent.y * 0.5f), rgba(235, 235, 245), caption);
	return x + key_size.x + 18 + extent.x + 22;
}

/* ---- generated surfaces of the 3D objects ---- */

struct SurfaceJob {
	const Game *game;
	const Art *art;
	const ShelfSystemStyle *style;
};

/* The lit marquee: the user's art, or the title on a gradient. */
void draw_marquee3d(ImDrawList *draw, void *opaque)
{
	const SurfaceJob *job = (const SurfaceJob *)opaque;
	const float W = CABINET3D_MARQUEE_W, H = CABINET3D_MARQUEE_H;
	if (job->art->marquee.id) {
		draw->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), rgba(0, 0, 0));
		draw->PushTextureID((ImTextureID)(uintptr_t)job->art->marquee.id);
		image_box(draw, job->art->marquee, ImVec2(0, 0), ImVec2(W, H), rgba(255, 255, 255), false);
		draw->PopTextureID();
		return;
	}
	const ImU32 a = job->style->marquee_a, b = job->style->marquee_b;
	draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H), a, b, fade(b, 0.7f), fade(a, 0.75f));
	draw->AddRectFilled(ImVec2(0, 0), ImVec2(W, H * 0.45f), rgba(255, 255, 255, 0.10f));
	centred_text(draw, s->font_title, 80.0f, 24, W - 24, H * 0.5f + 2, rgba(0, 0, 0, 0.35f), job->game->title.c_str());
	centred_text(draw, s->font_title, 80.0f, 24, W - 24, H * 0.5f - 2, rgba(255, 248, 225), job->game->title.c_str());
}

/* The screen: a black bezel around the 4:3 picture, and the glass. */
void draw_screen3d(ImDrawList *draw, void *opaque)
{
	const SurfaceJob *job = (const SurfaceJob *)opaque;
	const float W = CABINET3D_SCREEN_W, H = CABINET3D_SCREEN_H;
	const Version &version = job->game->versions[job->game->selected];
	draw->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), rgba(8, 8, 12));
	const float sh = H - 64.0f, sw = sh * 4.0f / 3.0f;
	const ImVec2 s0((W - sw) * 0.5f, 32.0f), s1((W + sw) * 0.5f, 32.0f + sh);
	if (job->art->snap.id) {
		/* Snaps keep the game's pixel size (512x240...); the monitor is 4:3. */
		draw->PushTextureID((ImTextureID)(uintptr_t)job->art->snap.id);
		draw->AddImage((ImTextureID)(uintptr_t)job->art->snap.id, s0, s1);
		draw->PopTextureID();
	} else {
		draw->AddRectFilledMultiColor(s0, s1, rgba(20, 40, 52), rgba(20, 40, 52), rgba(6, 14, 20), rgba(6, 14, 20));
		for (float y = s0.y; y < s1.y; y += 4.0f)
			draw->AddLine(ImVec2(s0.x, y), ImVec2(s1.x, y), rgba(0, 0, 0, 0.35f), 1.5f);
		centred_text(draw, s->font_title, 52.0f, s0.x + 20, s1.x - 20, (s0.y + s1.y) * 0.5f - 14, rgba(120, 235, 205),
			version.id.c_str());
		centred_text(draw, s->font_body, 26.0f, s0.x + 20, s1.x - 20, (s0.y + s1.y) * 0.5f + 34, rgba(120, 235, 205, 0.6f),
			"no snapshot");
	}
	/* Glass: a soft diagonal reflection. */
	const ImVec2 glass[4] = { ImVec2(s0.x, s0.y), ImVec2(s0.x + sw * 0.55f, s0.y), ImVec2(s0.x + sw * 0.25f, s1.y),
		ImVec2(s0.x, s1.y) };
	draw->AddConvexPolyFilled(glass, 4, rgba(255, 255, 255, 0.06f));
}

/* The disc label: the cover, or the title and system on a gradient. */
void draw_label3d(ImDrawList *draw, void *opaque)
{
	const SurfaceJob *job = (const SurfaceJob *)opaque;
	const float S = DISC3D_LABEL_SIZE;
	if (job->art->cover.id) {
		draw->PushTextureID((ImTextureID)(uintptr_t)job->art->cover.id);
		image_box(draw, job->art->cover, ImVec2(0, 0), ImVec2(S, S), rgba(255, 255, 255), true);
		draw->PopTextureID();
		return;
	}
	const ImU32 a = job->style->marquee_a, b = job->style->marquee_b;
	draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(S, S), a, b, fade(b, 0.6f), fade(a, 0.7f));
	for (int i = 0; i < 6; i++)
		draw->AddCircle(ImVec2(S * 0.5f, S * 0.5f), S * (0.24f + 0.04f * i), rgba(255, 255, 255, 0.05f), 96, 2.0f);
	centred_text(draw, s->font_title, 60.0f, S * 0.18f, S * 0.82f, S * 0.27f, rgba(255, 248, 225), job->game->title.c_str());
	centred_text(draw, s->font_body, 28.0f, S * 0.25f, S * 0.75f, S * 0.75f, rgba(255, 248, 225, 0.75f),
		job->game->system.c_str());
}

void ensure_surfaces(const Game &game, Art *art)
{
	SurfaceJob job = { &game, art, &style_for(game.system) };
	if (game.kind == SHELF_DISC) {
		if (!art->label3d.id)
			ui_gl_render_to_texture(&art->label3d, DISC3D_LABEL_SIZE, DISC3D_LABEL_SIZE, draw_label3d, &job);
		return;
	}
	if (!art->marquee3d.id)
		ui_gl_render_to_texture(&art->marquee3d, CABINET3D_MARQUEE_W, CABINET3D_MARQUEE_H, draw_marquee3d, &job);
	if (!art->screen3d.id)
		ui_gl_render_to_texture(&art->screen3d, CABINET3D_SCREEN_W, CABINET3D_SCREEN_H, draw_screen3d, &job);
}

/* ---- 3D carousel ---- */

/* The carousel area of the screen, in pixels. */
const float kStageX = 0.0f, kStageY = 44.0f, kStageW = 584.0f;
const float kEye[3] = { 0.0f, 1.22f, 4.25f };

float stage_height()
{
	return (float)s->height - kStageY - 44.0f;
}

void stage_camera(float view_projection[16])
{
	float projection[16], view[16];
	const float target[3] = { 0.0f, 0.97f, 0.0f }, up[3] = { 0.0f, 1.0f, 0.0f };
	mat4_perspective(projection, 32.0f, kStageW / stage_height(), 0.1f, 40.0f);
	mat4_look_at(view, kEye, target, up);
	mat4_multiply(view_projection, projection, view);
}

unsigned kick_for(const std::string &system)
{
	for (const auto &entry : s->kick)
		if (entry.first == system)
			return entry.second;
	const ShelfSystemStyle &style = style_for(system);
	s->kick.push_back(std::make_pair(system, cabinet3d_kick_texture(style.kick_top, style.kick_bottom)));
	return s->kick.back().second;
}

const float kLaunch = 0.9f; /* seconds of the launch animation */

float ease_out(float t)
{
	t = std::max(0.0f, std::min(1.0f, t));
	return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

float ease_in(float t)
{
	t = std::max(0.0f, std::min(1.0f, t));
	return t * t * t;
}

/* The selected object turns a little to show its side, sways gently and
 * follows the right stick; neighbours step back and turn toward the middle.
 * Discs lean back, as in PCSX AwO's selector, to show their face.
 *
 * Launched, it is inserted, as in the Socket frontend: a disc rises,
 * stands upright facing the viewer, then goes straight down into a slot
 * in the floor; a cabinet comes toward the viewer. */
void place(float model[16], int index, ShelfItemKind kind, float *yaw_out, float *pitch_out, float *spin_out)
{
	const float d = (float)index - s->scroll, distance = fabsf(d);
	const float focus = std::max(0.0f, 1.0f - distance * 2.0f);
	const float sway = 4.0f * sinf(s->time * 2.0f * (float)M_PI / 6.0f) * focus;
	const bool disc = kind == SHELF_DISC;
	float yaw = (disc ? -7.0f - d * 18.0f : -10.0f - d * 30.0f) + sway + s->yaw * focus;
	float pitch = (disc ? -25.0f : 0.0f) + s->pitch * focus;
	float x = d * 1.32f, y = 0.0f, z = -std::min(distance, 2.5f) * 1.35f, spin = 0.0f, scale = 1.0f;
	if (index == s->game && s->launch_time >= 0.0f) {
		const float l = std::min(1.0f, s->launch_time / kLaunch);
		if (disc) {
			const float rise = ease_out(l / 0.35f), drop = ease_in((l - 0.35f) / 0.6f);
			y += 0.45f * rise - (0.45f + DISC3D_CENTER_Y + DISC3D_RADIUS + 0.05f) * drop;
			yaw += (0.0f - yaw) * rise;
			pitch += (0.0f - pitch) * rise;
		} else {
			z += 2.6f * l * l;
			y -= 0.35f * l * l;
		}
	}
	mat4_place_tilted(model, x, y, z, yaw, pitch, disc ? DISC3D_CENTER_Y : 0.95f, spin, scale);
	if (yaw_out) *yaw_out = yaw;
	if (pitch_out) *pitch_out = pitch;
	if (spin_out) *spin_out = spin;
}

void draw_stage_3d()
{
	float view_projection[16], model[16];
	std::vector<int> visible;
	for (int i = 0; i < (int)s->games.size(); i++)
		if (fabsf((float)i - s->scroll) < 2.8f)
			visible.push_back(i);
	for (int i : visible)
		ensure_surfaces(s->games[i], art_for(s->games[i]));
	stage_camera(view_projection);
	ui_gl3d_begin((int)kStageX, (int)kStageY, (int)kStageW, (int)stage_height(), s->height, view_projection, kEye);
	/* Shadows first, then the objects. */
	for (int pass = 0; pass < 2; pass++) for (int i : visible) {
		const Game &game = s->games[i];
		const float d = (float)i - s->scroll;
		const float light = std::max(0.28f, 1.0f - 0.36f * fabsf(d));
		float yaw = 0.0f, pitch = 0.0f, spin = 0.0f;
		place(model, i, game.kind, &yaw, &pitch, &spin);
		if (pass == 0) {
			cabinet3d_shadow(model, std::max(0.3f, 1.0f - 0.3f * fabsf(d)));
			continue;
		}
		Art *art = art_for(game);
		const ShelfSystemStyle &style = style_for(game.system);
		if (game.kind == SHELF_DISC) {
			DiscLook look = { art->label3d.id, yaw, pitch, spin, i == s->game && s->launch_time >= 0.0f, light };
			disc3d_draw(model, &look);
			continue;
		}
		const float focus = std::max(0.0f, 1.0f - fabsf(d) * 2.0f);
		CabinetLook look = { art->marquee3d.id, art->screen3d.id, kick_for(game.system), style.body, style.trim, light,
			/* The selected marquee breathes, as a lit tube does. */
			1.0f - focus * (0.07f + 0.07f * sinf(s->time * 2.0f * (float)M_PI / 2.5f)) };
		cabinet3d_draw(model, &look);
	}
	ui_gl3d_end();
}

void draw_background(ImDrawList *draw)
{
	const float W = (float)s->width, H = (float)s->height;
	float view_projection[16], x, y;
	const float floor_point[3] = { 0.0f, 0.0f, 0.0f };
	draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(W, H), rgba(13, 16, 32), rgba(13, 16, 32),
		rgba(26, 31, 58), rgba(26, 31, 58));
	if (s->games.empty())
		return;
	/* A pool of light on the floor of the stage. */
	stage_camera(view_projection);
	if (mat4_project(view_projection, floor_point, kStageX, kStageY, kStageW, stage_height(), &x, &y))
		for (int i = 0; i < 48; i++)
			draw->AddCircleFilled(ImVec2(x, y), 260.0f - i * 5.0f, rgba(90, 100, 200, 0.006f), 64);
}

void draw_frame(ImDrawList *draw)
{
	const float W = (float)s->width, H = (float)s->height;

	if (s->games.empty()) {
		centred_text(draw, s->font_title, 26.0f, 40, W - 40, H * 0.42f, rgba(235, 235, 245), "No games found");
		centred_text(draw, s->font_body, 17.0f, 40, W - 40, H * 0.42f + 40, rgba(170, 175, 200), s->empty_hint.c_str());
		hint(draw, 32, H - 24, SHELF_CIRCLE, "Exit");
		return;
	}

	/* Header: the application's name, the position, the system. */
	const std::string brand = s->title + (s->title_accent.empty() ? "" : " ");
	const ImVec2 lead = s->font_body->CalcTextSizeA(20.0f, FLT_MAX, 0.0f, brand.c_str());
	draw->AddText(s->font_body, 20.0f, ImVec2(32, 15), rgba(240, 240, 248), brand.c_str());
	draw->AddText(s->font_body, 20.0f, ImVec2(32 + lead.x, 15), rgba(255, 180, 0), s->title_accent.c_str());
	const ImVec2 accent = s->font_body->CalcTextSizeA(20.0f, FLT_MAX, 0.0f, s->title_accent.c_str());
	char count[64];
	snprintf(count, sizeof(count), "%d / %d", s->game + 1, (int)s->games.size());
	draw->AddText(s->font_small, 15.0f, ImVec2(32 + lead.x + accent.x + 16, 19), rgba(150, 155, 185), count);
	const Game &game = s->games[s->game];
	const ImVec2 chip = s->font_small->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, game.system.c_str());
	draw->AddRectFilled(ImVec2(W - 32 - chip.x - 20, 16), ImVec2(W - 32, 38), rgba(230, 57, 70, 0.22f), 11.0f);
	draw->AddText(s->font_small, 13.0f, ImVec2(W - 32 - chip.x - 10, 27 - chip.y * 0.5f), rgba(255, 160, 165),
		game.system.c_str());

	/* Info panel: title, versions. */
	const Version &version = game.versions[game.selected];
	const float px = 604.0f, pw = W - px - 32.0f;
	draw->AddRectFilled(ImVec2(px - 16, 56), ImVec2(W - 16, H - 56), rgba(24, 29, 54), 10.0f);
	draw->AddRect(ImVec2(px - 16, 56), ImVec2(W - 16, H - 56), rgba(255, 255, 255, 0.06f), 10.0f);
	draw->AddText(s->font_title, 30.0f, ImVec2(px, 70), rgba(245, 245, 250), game.title.c_str(), NULL, pw);
	const ImVec2 title_size = s->font_title->CalcTextSizeA(30.0f, FLT_MAX, pw, game.title.c_str());
	char meta[160];
	if (version.year > 0)
		snprintf(meta, sizeof(meta), "%d  \xc2\xb7  %s", version.year, game.system.c_str());
	else
		snprintf(meta, sizeof(meta), "%s", game.system.c_str());
	draw->AddText(s->font_body, 17.0f, ImVec2(px, 76 + title_size.y), rgba(160, 165, 195), meta);

	float y = 118 + title_size.y;
	draw->AddText(s->font_small, 13.0f, ImVec2(px, y), rgba(255, 180, 0), "VERSIONS");
	y += 22;
	const int rows = std::min((int)game.versions.size(), 6);
	const int first = std::max(0, std::min(game.selected - rows / 2, (int)game.versions.size() - rows));
	for (int i = first; i < first + rows; i++) {
		const Version &v = game.versions[i];
		const bool selected = i == game.selected;
		const ImVec2 r0(px - 6, y), r1(W - 26, y + 42);
		if (selected) {
			draw->AddRectFilled(r0, r1, rgba(255, 180, 0, 0.16f), 6.0f);
			draw->AddRectFilled(r0, ImVec2(r0.x + 3, r1.y), rgba(255, 180, 0), 2.0f);
		}
		draw->AddText(s->font_body, 17.0f, ImVec2(px + 8, y + 4), selected ? rgba(255, 255, 255) : rgba(200, 204, 225),
			v.label.empty() ? v.id.c_str() : v.label.c_str());
		draw->AddText(s->font_small, 13.0f, ImVec2(px + 8, y + 23), selected ? rgba(255, 200, 90) : rgba(130, 135, 165),
			v.id.c_str());
		y += 46;
	}
	if ((int)game.versions.size() > rows) {
		char more[32];
		snprintf(more, sizeof(more), "%d / %d", game.selected + 1, (int)game.versions.size());
		draw->AddText(s->font_small, 13.0f, ImVec2(px, y + 2), rgba(130, 135, 165), more);
	}

	/* Footer hints. */
	const float fy = H - 26;
	float x = 32;
	x = hint(draw, x, fy, SHELF_CROSS, "Play");
	x = text_hint(draw, x, fy, "< >", "Game");
	if (game.versions.size() > 1)
		x = text_hint(draw, x, fy, "^ v", "Version");
#ifdef ARCADE_SHELF_ADB
	x = hint(draw, x, fy, SHELF_TRIANGLE, "Artwork");
#endif
	hint(draw, W - 110, fy, SHELF_CIRCLE, "Exit");
}

bool pressed(unsigned buttons, unsigned button, float dt)
{
	const bool now = (buttons & button) != 0, before = (s->previous & button) != 0;
	if (now && !before) {
		s->repeat_button = button;
		s->repeat_time = 0.32f;
		return true;
	}
	if (now && s->repeat_button == button && (button & (SHELF_UP | SHELF_DOWN | SHELF_LEFT | SHELF_RIGHT))) {
		s->repeat_time -= dt;
		if (s->repeat_time <= 0.0f) {
			s->repeat_time += 0.085f;
			return true;
		}
	}
	return false;
}

#ifdef ARCADE_SHELF_ADB
/* ---- artwork download (Arcade Database), arcade games only ---- */

std::vector<std::string> adb_sets()
{
	std::vector<std::string> sets;
	for (const Game &game : s->games) {
		if (game.kind != SHELF_CABINET)
			continue;
		sets.push_back(game.key);
		for (const Version &version : game.versions)
			if (version.id != game.key)
				sets.push_back(version.id);
	}
	return sets;
}

void draw_adb_panel(ImDrawList *draw)
{
	const float W = (float)s->width, H = (float)s->height;
	const AdbProgress progress = adb_scraper_progress();
	const ImVec2 p0(W * 0.5f - 300, H * 0.5f - 140), p1(W * 0.5f + 300, H * 0.5f + 140);
	char line[256];
	draw->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), rgba(5, 7, 16, 0.72f));
	draw->AddRectFilled(p0, p1, rgba(24, 29, 54), 12.0f);
	draw->AddRect(p0, p1, rgba(255, 180, 0, 0.5f), 12.0f);
	draw->AddText(s->font_title, 26.0f, ImVec2(p0.x + 28, p0.y + 22), rgba(245, 245, 250), "Download artwork");
	snprintf(line, sizeof(line), "Snapshots, titles and marquees for %d sets.", (int)adb_sets().size());
	draw->AddText(s->font_body, 17.0f, ImVec2(p0.x + 28, p0.y + 66), rgba(200, 204, 225), line);
	draw->AddText(s->font_body, 17.0f, ImVec2(p0.x + 28, p0.y + 88), rgba(200, 204, 225),
		"Only missing files are downloaded.");
	draw->AddText(s->font_small, 13.0f, ImVec2(p0.x + 28, p0.y + 122), rgba(255, 180, 0), "SOURCE");
	draw->AddText(s->font_small, 15.0f, ImVec2(p0.x + 28, p0.y + 140), rgba(160, 165, 195), ADB_CREDIT, NULL, 540);
	if (progress.running || progress.finished) {
		const float fraction = progress.sets_total ? (float)progress.sets_done / progress.sets_total : 0.0f;
		const ImVec2 b0(p0.x + 28, p0.y + 182), b1(p1.x - 28, p0.y + 194);
		draw->AddRectFilled(b0, b1, rgba(255, 255, 255, 0.08f), 6.0f);
		draw->AddRectFilled(b0, ImVec2(b0.x + (b1.x - b0.x) * fraction, b1.y), rgba(255, 180, 0), 6.0f);
		if (progress.running)
			snprintf(line, sizeof(line), "%s  -  %d images", progress.current.c_str(), progress.images);
		else
			snprintf(line, sizeof(line), "%s: %d images downloaded", progress.cancelled ? "Stopped" : "Done",
				progress.images);
		draw->AddText(s->font_small, 15.0f, ImVec2(b0.x, b1.y + 10), rgba(200, 204, 225), line);
		if (!progress.error.empty())
			draw->AddText(s->font_small, 13.0f, ImVec2(b0.x, b1.y + 30), rgba(255, 120, 120), progress.error.c_str());
	}
	float x = p0.x + 28;
	if (!progress.running)
		x = hint(draw, x, p1.y - 26, SHELF_CROSS, progress.finished ? "Again" : "Download");
	hint(draw, x, p1.y - 26, SHELF_CIRCLE, progress.running ? "Stop" : "Close");
}

/* The panel takes the input while it is open. */
void adb_input(unsigned buttons, float dt)
{
	const AdbProgress progress = adb_scraper_progress();
	if (pressed(buttons, SHELF_CROSS, dt) && !progress.running) {
		const std::string ca = s->data_root + "/cacert.pem";
		const std::string agent = s->title + " " + s->title_accent + " (arcade-shelf)";
		struct stat st;
		adb_scraper_start(s->data_root, adb_sets(), stat(ca.c_str(), &st) == 0 ? ca : std::string(), agent);
	}
	if (pressed(buttons, SHELF_CIRCLE, dt)) {
		if (progress.running)
			adb_scraper_cancel();
		else
			s->adb_open = false;
	}
}
#endif

} // namespace

int shelf_init(const ShelfConfig *config)
{
	ImFontConfig font_config;
	s = new State();
	s->launch_time = -1.0f;
	s->title = config->title ? config->title : "arcade-shelf";
	s->title_accent = config->title_accent ? config->title_accent : "";
	s->data_root = config->data_root ? config->data_root : ".";
	s->empty_hint = config->empty_hint ? config->empty_hint : "";
	s->width = config->width;
	s->height = config->height;
	for (int i = 0; i < config->style_count; i++)
		s->style_names.push_back(config->styles[i].system ? config->styles[i].system : "");
	for (int i = 0; i < config->style_count; i++) {
		s->styles.push_back(config->styles[i]);
		s->styles.back().system = s->style_names[i].c_str();
	}
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = NULL;
	io.DisplaySize = ImVec2((float)config->width, (float)config->height);
	font_config.FontDataOwnedByAtlas = false;
	font_config.OversampleH = 2;
	s->font_small = io.Fonts->AddFontFromMemoryTTF(Roboto_Medium_ttf, sizeof(Roboto_Medium_ttf), 15.0f, &font_config);
	s->font_body = io.Fonts->AddFontFromMemoryTTF(Roboto_Medium_ttf, sizeof(Roboto_Medium_ttf), 18.0f, &font_config);
	s->font_title = io.Fonts->AddFontFromMemoryTTF(Roboto_Medium_ttf, sizeof(Roboto_Medium_ttf), 30.0f, &font_config);
	return ui_gl_init() && cabinet3d_init() && disc3d_init();
}

void shelf_set_games(const ShelfGame *games, int count)
{
	if (!s->games.empty())
		save_config();
	free_art();
	s->games.clear();
	for (int i = 0; i < count; i++) {
		const ShelfGame &in = games[i];
		if (in.version_count <= 0)
			continue;
		Game game;
		game.key = in.key ? in.key : "";
		game.title = in.title ? in.title : game.key;
		game.system = in.system ? in.system : "";
		game.kind = in.kind;
		game.selected = 0;
		for (int v = 0; v < in.version_count; v++) {
			const ShelfVersion &version = in.versions[v];
			const char *id = version.id ? version.id : "";
			game.versions.push_back(Version{ id, version.label ? version.label : "",
				version.launch ? version.launch : "", version.year, version.art && *version.art ? version.art : id });
		}
		s->games.push_back(game);
	}
	s->game = 0;
	load_config();
	s->scroll = (float)s->game;
	s->posed_game = -1;
	s->launch_time = -1.0f;
}

void shelf_shutdown(void)
{
	if (!s)
		return;
#ifdef ARCADE_SHELF_ADB
	adb_scraper_cancel();
	adb_scraper_join();
#endif
	save_config();
	free_art();
	for (auto &entry : s->kick) {
		UiTexture texture = { entry.second, 0, 0 };
		ui_gl_free_texture(&texture);
	}
	disc3d_shutdown();
	cabinet3d_shutdown();
	ui_gl_shutdown();
	ImGui::DestroyContext();
	delete s;
	s = NULL;
}

int shelf_frame(const ShelfInput *input, char *launch, size_t launch_size)
{
	int action = SHELF_NONE;
	unsigned buttons = input->buttons;
	ImGuiIO &io = ImGui::GetIO();
	s->frame++;
	io.DeltaTime = input->dt > 0.0f ? input->dt : 1.0f / 60.0f;
	s->time += io.DeltaTime;

#ifdef ARCADE_SHELF_ADB
	{
		/* New files on disk: reload the art once a download ends. */
		static bool was_running;
		const bool running = adb_scraper_progress().running;
		if (was_running && !running)
			free_art();
		was_running = running;
	}
	if (s->adb_open) {
		adb_input(buttons, io.DeltaTime);
		buttons = 0;
	} else if (pressed(buttons, SHELF_TRIANGLE, io.DeltaTime) && !s->games.empty()) {
		s->adb_open = true;
		buttons = 0;
	}
#endif
	/* The launch animation plays to its end, then the game is returned. */
	if (s->launch_time >= 0.0f) {
		s->launch_time += io.DeltaTime;
		if (s->launch_time >= kLaunch) {
			snprintf(launch, launch_size, "%s", s->launch.c_str());
			s->launch_time = -1.0f;
			action = SHELF_LAUNCH;
		}
		buttons = 0;
	}
	if (pressed(buttons, SHELF_CIRCLE, io.DeltaTime))
		action = SHELF_QUIT;
	if (!s->games.empty()) {
		Game &game = s->games[s->game];
		if (pressed(buttons, SHELF_LEFT, io.DeltaTime) && s->game > 0)
			s->game--;
		if (pressed(buttons, SHELF_RIGHT, io.DeltaTime) && s->game + 1 < (int)s->games.size())
			s->game++;
		if (pressed(buttons, SHELF_UP, io.DeltaTime) && game.selected > 0)
			game.selected--;
		if (pressed(buttons, SHELF_DOWN, io.DeltaTime) && game.selected + 1 < (int)game.versions.size())
			game.selected++;
		if (pressed(buttons, SHELF_CROSS, io.DeltaTime) || pressed(buttons, SHELF_START, io.DeltaTime)) {
			const Game &chosen = s->games[s->game];
			s->launch = chosen.versions[chosen.selected].launch;
			s->launch_time = 0.0f;
			save_config();
		}
	}
	s->previous = buttons;
	/* The right stick turns the selected object; another game starts from
	 * its rest pose. */
	if (s->posed_game != s->game) {
		s->posed_game = s->game;
		s->yaw = s->pitch = 0.0f;
	}
	if (fabsf(input->stick_x) > 0.2f)
		s->yaw = fmodf(s->yaw + input->stick_x * 150.0f * io.DeltaTime, 360.0f);
	if (fabsf(input->stick_y) > 0.2f)
		/* Up brings the top toward the viewer, to look at the control panel. */
		s->pitch = std::max(-15.0f, std::min(15.0f, s->pitch - input->stick_y * 45.0f * io.DeltaTime));
	/* Ease the carousel toward the selected game. */
	s->scroll += ((float)s->game - s->scroll) * std::min(1.0f, io.DeltaTime * 12.0f);
	if (fabsf((float)s->game - s->scroll) < 0.001f)
		s->scroll = (float)s->game;

	{
		ImDrawList background(ImGui::GetDrawListSharedData());
		ImDrawList *lists[1] = { &background };
		background.PushClipRect(ImVec2(0, 0), io.DisplaySize);
		background.PushTextureID(io.Fonts->TexID);
		draw_background(&background);
		ui_gl_render_lists(lists, 1, s->width, s->height);
	}
	if (!s->games.empty())
		draw_stage_3d();

	ImGui::NewFrame();
	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(io.DisplaySize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::SetNextWindowBgAlpha(0.0f);
	ImGui::Begin("##shelf", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs);
	draw_frame(ImGui::GetWindowDrawList());
	if (s->launch_time >= 0.0f) {
		/* Into the game: fade to black over the end of the launch. */
		const float l = std::min(1.0f, s->launch_time / kLaunch);
		const float fade_in = std::max(0.0f, (l - 0.45f) / 0.55f);
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, rgba(0, 0, 0, fade_in * fade_in));
	}
#ifdef ARCADE_SHELF_ADB
	if (s->adb_open)
		draw_adb_panel(ImGui::GetWindowDrawList());
#endif
	ImGui::End();
	ImGui::PopStyleVar(2);
	ImGui::Render();
	ui_gl_render(ImGui::GetDrawData(), s->width, s->height);
	return action;
}
