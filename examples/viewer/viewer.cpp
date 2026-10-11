/* arcade-shelf viewer: the selector on macOS (SDL3, OpenGL 2.1), with a
 * catalogue from a text file, to work on the UI and to check it with
 * scripted screenshots.
 *
 *   build/viewer [--catalog FILE] [--data DIR]
 *       [--script STEPS] [--step-frames N] [--shot FILE.png] [--shots DIR] [--headless]
 *
 * Keys: arrows; Z or Enter = cross (play); X or Esc = circle (exit);
 * S = triangle; A = square; Q/W = L/R; J L I K = right stick (turns the
 * cabinet). A game controller works too.
 * --script runs a comma-separated button list (U D L R X O T S LB RB ST SE,
 * or "-" for no button), one press every --step-frames frames, then exits;
 * SL SR SU SD hold the right stick for a whole step.
 * --shots saves a PNG after each step; --shot saves the last frame.
 * --headless draws into an offscreen framebuffer instead of a window.
 *
 * Catalogue file, tab-separated; "#" starts a comment:
 *   game    <key> <title> <system> <cabinet|disc>
 *   version <id> <label> <launch> <year>     (versions of the last game)
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#include <SDL3/SDL.h>
#define GL_SILENCE_DEPRECATION
#define GL_GLEXT_PROTOTYPES
#include <OpenGL/gl.h>
#include <OpenGL/glext.h>
#include <png.h>

#include <fstream>
#include <sstream>

#include "arcade_shelf.h"

enum { WIDTH = 960, HEIGHT = 544 };

/* Colours per system, as an application would give them. */
static const ShelfSystemStyle kStyles[] = {
	{ "Namco System 12", 0xffa54840, 0xff343ee8, 0xff6e302c, 0xff3c1816, 0xff5046f0, 0xffc83282 },
	{ "Namco System 11", 0xff2e24a0, 0xff201c1c, 0xff241a78, 0xff120c3c, 0xff2878fa, 0xff321ec8 },
	{ "Capcom ZN", 0xff322c2c, 0xff1ec4fa, 0xff2a2424, 0xff141010, 0xff28c8fa, 0xff1e46dc },
	{ "Taito FX-1", 0xffbe6028, 0xfff0ebeb, 0xff96481e, 0xff461e0c, 0xffffa03c, 0xffb43c1e },
	{ "Konami GV", 0xff4c7822, 0xff1e82f5, 0xff3a5c1a, 0xff18280a, 0xff2896fa, 0xff5aa028 },
	{ "Sony PlayStation", 0xff4a4440, 0xffb8b4b0, 0xff3a3634, 0xff1a1816, 0xff3a2ad0, 0xffd08a20 },
};

/* The catalogue file; strings live in the vectors. */
struct Catalogue {
	std::vector<std::vector<std::string> > game_fields, version_fields;
	std::vector<std::vector<ShelfVersion> > versions;
	std::vector<ShelfGame> games;
};

static std::vector<std::string> split_tabs(const std::string &line)
{
	std::vector<std::string> fields;
	std::stringstream stream(line);
	std::string field;
	while (std::getline(stream, field, '\t'))
		fields.push_back(field);
	return fields;
}

static bool load_catalogue(const char *path, Catalogue *c)
{
	std::ifstream file(path);
	std::string line;
	std::vector<int> owner;
	if (!file)
		return false;
	while (std::getline(file, line)) {
		if (line.empty() || line[0] == '#')
			continue;
		std::vector<std::string> fields = split_tabs(line);
		if (fields[0] == "game" && fields.size() >= 5)
			c->game_fields.push_back(fields);
		else if (fields[0] == "version" && fields.size() >= 5 && !c->game_fields.empty()) {
			c->version_fields.push_back(fields);
			owner.push_back((int)c->game_fields.size() - 1);
		}
	}
	c->versions.resize(c->game_fields.size());
	for (size_t i = 0; i < c->version_fields.size(); i++) {
		const std::vector<std::string> &f = c->version_fields[i];
		c->versions[owner[i]].push_back(ShelfVersion{ f[1].c_str(), f[2].c_str(), f[3].c_str(), atoi(f[4].c_str()) });
	}
	for (size_t i = 0; i < c->game_fields.size(); i++) {
		const std::vector<std::string> &f = c->game_fields[i];
		c->games.push_back(ShelfGame{ f[1].c_str(), f[2].c_str(), f[3].c_str(),
			f[4] == "disc" ? SHELF_DISC : SHELF_CABINET, c->versions[i].data(), (int)c->versions[i].size() });
	}
	return true;
}

/* Script steps that hold the right stick for the whole step. */
static bool script_stick(const std::string &name, float *x, float *y)
{
	if (name == "SL") { *x = -1; *y = 0; return true; }
	if (name == "SR") { *x = 1; *y = 0; return true; }
	if (name == "SU") { *x = 0; *y = -1; return true; }
	if (name == "SD") { *x = 0; *y = 1; return true; }
	return false;
}

static unsigned script_button(const std::string &name)
{
	static const struct { const char *name; unsigned button; } names[] = {
		{"U", SHELF_UP}, {"D", SHELF_DOWN}, {"L", SHELF_LEFT}, {"R", SHELF_RIGHT}, {"X", SHELF_CROSS},
		{"O", SHELF_CIRCLE}, {"T", SHELF_TRIANGLE}, {"S", SHELF_SQUARE}, {"LB", SHELF_L}, {"RB", SHELF_R},
		{"ST", SHELF_START}, {"SE", SHELF_SELECT}, {"-", 0},
	};
	for (const auto &entry : names)
		if (name == entry.name)
			return entry.button;
	fprintf(stderr, "viewer: unknown script button '%s'\n", name.c_str());
	exit(2);
}

static bool save_png(const char *path)
{
	std::vector<unsigned char> pixels(WIDTH * HEIGHT * 4), flipped(WIDTH * HEIGHT * 4);
	png_image image;
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	for (int y = 0; y < HEIGHT; y++)
		memcpy(&flipped[y * WIDTH * 4], &pixels[(HEIGHT - 1 - y) * WIDTH * 4], WIDTH * 4);
	memset(&image, 0, sizeof(image));
	image.version = PNG_IMAGE_VERSION;
	image.width = WIDTH;
	image.height = HEIGHT;
	image.format = PNG_FORMAT_RGBA;
	if (!png_image_write_to_file(&image, path, 0, flipped.data(), 0, NULL)) {
		fprintf(stderr, "viewer: cannot write %s\n", path);
		return false;
	}
	return true;
}

static unsigned keyboard_buttons(void)
{
	const bool *keys = SDL_GetKeyboardState(NULL);
	unsigned buttons = 0;
	if (keys[SDL_SCANCODE_UP]) buttons |= SHELF_UP;
	if (keys[SDL_SCANCODE_DOWN]) buttons |= SHELF_DOWN;
	if (keys[SDL_SCANCODE_LEFT]) buttons |= SHELF_LEFT;
	if (keys[SDL_SCANCODE_RIGHT]) buttons |= SHELF_RIGHT;
	if (keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_RETURN]) buttons |= SHELF_CROSS;
	if (keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_ESCAPE]) buttons |= SHELF_CIRCLE;
	if (keys[SDL_SCANCODE_S]) buttons |= SHELF_TRIANGLE;
	if (keys[SDL_SCANCODE_A]) buttons |= SHELF_SQUARE;
	if (keys[SDL_SCANCODE_Q]) buttons |= SHELF_L;
	if (keys[SDL_SCANCODE_W]) buttons |= SHELF_R;
	return buttons;
}

static unsigned gamepad_buttons(SDL_Gamepad *pad)
{
	static const struct { SDL_GamepadButton button; unsigned shelf; } map[] = {
		{SDL_GAMEPAD_BUTTON_DPAD_UP, SHELF_UP}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, SHELF_DOWN},
		{SDL_GAMEPAD_BUTTON_DPAD_LEFT, SHELF_LEFT}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SHELF_RIGHT},
		{SDL_GAMEPAD_BUTTON_SOUTH, SHELF_CROSS}, {SDL_GAMEPAD_BUTTON_EAST, SHELF_CIRCLE},
		{SDL_GAMEPAD_BUTTON_NORTH, SHELF_TRIANGLE}, {SDL_GAMEPAD_BUTTON_WEST, SHELF_SQUARE},
		{SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SHELF_L}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SHELF_R},
		{SDL_GAMEPAD_BUTTON_START, SHELF_START}, {SDL_GAMEPAD_BUTTON_BACK, SHELF_SELECT},
	};
	unsigned buttons = 0;
	if (!pad)
		return 0;
	for (const auto &entry : map)
		if (SDL_GetGamepadButton(pad, entry.button))
			buttons |= entry.shelf;
	return buttons;
}

int main(int argc, char **argv)
{
	const char *catalog = "examples/viewer/demo_catalog.tsv", *data = "build/viewer-data", *shot = NULL, *shots = NULL;
	std::vector<unsigned> script;
	std::vector<std::pair<float, float> > script_sticks;
	int step_frames = 15, headless = 0, result = 0;
	for (int i = 1; i < argc; i++) {
		const std::string arg = argv[i];
		const bool has_value = i + 1 < argc;
		if (arg == "--catalog" && has_value) catalog = argv[++i];
		else if (arg == "--data" && has_value) data = argv[++i];
		else if (arg == "--headless") headless = 1;
		else if (arg == "--shot" && has_value) shot = argv[++i];
		else if (arg == "--shots" && has_value) shots = argv[++i];
		else if (arg == "--step-frames" && has_value) step_frames = atoi(argv[++i]);
		else if (arg == "--script" && has_value) {
			std::string list = argv[++i];
			size_t start = 0;
			while (start <= list.size()) {
				size_t comma = list.find(',', start);
				const std::string token = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
				float x = 0, y = 0;
				const bool stick = script_stick(token, &x, &y);
				script.push_back(stick ? 0 : script_button(token));
				script_sticks.push_back(std::make_pair(x, y));
				if (comma == std::string::npos)
					break;
				start = comma + 1;
			}
		} else {
			fprintf(stderr, "usage: see the comment at the top of examples/viewer/viewer.cpp\n");
			return 2;
		}
	}
	const bool scripted = !script.empty() || shot;

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
		fprintf(stderr, "viewer: SDL_Init: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_Window *window = SDL_CreateWindow("arcade-shelf", WIDTH, HEIGHT,
		SDL_WINDOW_OPENGL | (headless ? SDL_WINDOW_HIDDEN : 0));
	SDL_GLContext context = window ? SDL_GL_CreateContext(window) : NULL;
	if (!context) {
		fprintf(stderr, "viewer: no GL context: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GL_SetSwapInterval(scripted ? 0 : 1);

	GLuint framebuffer = 0, color = 0, depth = 0;
	if (headless) {
		glGenFramebuffersEXT(1, &framebuffer);
		glGenRenderbuffersEXT(1, &color);
		glGenRenderbuffersEXT(1, &depth);
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, color);
		glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_RGBA8, WIDTH, HEIGHT);
		glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, depth);
		glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT24, WIDTH, HEIGHT);
		glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, framebuffer);
		glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_RENDERBUFFER_EXT, color);
		glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, depth);
	}

	SDL_CreateDirectory(data);
	Catalogue catalogue;
	if (!load_catalogue(catalog, &catalogue)) {
		fprintf(stderr, "viewer: cannot read %s\n", catalog);
		return 1;
	}
	ShelfConfig config = { "arcade", "shelf", data, "Add games to the catalogue file.", WIDTH, HEIGHT,
		kStyles, (int)(sizeof(kStyles) / sizeof(kStyles[0])) };
	if (!shelf_init(&config)) {
		fprintf(stderr, "viewer: init failed\n");
		return 1;
	}
	shelf_set_games(catalogue.games.data(), (int)catalogue.games.size());

	SDL_Gamepad *pad = NULL;
	Uint64 last = SDL_GetTicksNS();
	size_t step = 0;
	int frame = 0;
	for (bool running = true; running;) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			if (event.type == SDL_EVENT_QUIT)
				running = false;
			else if (event.type == SDL_EVENT_GAMEPAD_ADDED && !pad)
				pad = SDL_OpenGamepad(event.gdevice.which);
		}
		unsigned buttons;
		float dt, stick_x = 0, stick_y = 0;
		if (scripted) {
			/* Fixed time step: the same script always draws the same frames. */
			dt = 1.0f / 60.0f;
			buttons = step < script.size() && frame % step_frames == 0 ? script[step] : 0;
			if (step < script.size()) {
				stick_x = script_sticks[step].first;
				stick_y = script_sticks[step].second;
			}
		} else {
			const Uint64 now = SDL_GetTicksNS();
			const bool *keys = SDL_GetKeyboardState(NULL);
			dt = (float)((now - last) / 1e9);
			last = now;
			buttons = keyboard_buttons() | gamepad_buttons(pad);
			/* Right stick, or J L I K. */
			if (pad) {
				stick_x = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
				stick_y = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;
			}
			if (keys[SDL_SCANCODE_J]) stick_x = -1;
			if (keys[SDL_SCANCODE_L]) stick_x = 1;
			if (keys[SDL_SCANCODE_I]) stick_y = -1;
			if (keys[SDL_SCANCODE_K]) stick_y = 1;
		}
		char launch[1024];
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		const ShelfInput input = { buttons, stick_x, stick_y, dt };
		const int action = shelf_frame(&input, launch, sizeof(launch));
		frame++;
		if (scripted && frame % step_frames == 0) {
			if (shots) {
				char path[1024];
				snprintf(path, sizeof(path), "%s/step-%02zu.png", shots, step);
				save_png(path);
			}
			if (++step >= script.size()) {
				if (shot && !save_png(shot))
					result = 1;
				running = false;
			}
		}
		if (action == SHELF_LAUNCH) {
			printf("launch %s\n", launch);
			if (!scripted)
				running = false;
		} else if (action == SHELF_QUIT && !scripted) {
			running = false;
		}
		if (!headless)
			SDL_GL_SwapWindow(window);
	}
	shelf_shutdown();
	if (framebuffer) {
		glDeleteFramebuffersEXT(1, &framebuffer);
		glDeleteRenderbuffersEXT(1, &color);
		glDeleteRenderbuffersEXT(1, &depth);
	}
	SDL_GL_DestroyContext(context);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return result;
}
