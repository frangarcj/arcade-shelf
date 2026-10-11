#include "disc3d.h"

#include <math.h>
#include <vector>

#include <imgui.h>

#include "gl.h"

namespace {

std::vector<UiVertex3D> s_label, s_hub, s_data, s_rim;

const float kHole = 0.075f, kHub = 0.20f, kThickness = 0.012f;
const int kSegments = 72;

void push(std::vector<UiVertex3D> &out, float x, float y, float z, const float n[3], float u, float v, ImU32 color)
{
	UiVertex3D vertex = { x, y, z, n[0], n[1], n[2], u, v, color };
	out.push_back(vertex);
}

/* A flat ring on the face at z, between radii r0 and r1; the texture maps
 * the square [-R, R] onto the face. */
void ring(std::vector<UiVertex3D> &out, float r0, float r1, float z, float nz, bool hue)
{
	const float n[3] = { 0.0f, 0.0f, nz };
	for (int i = 0; i < kSegments; i++) {
		const float a0 = 2.0f * (float)M_PI * i / kSegments, a1 = 2.0f * (float)M_PI * (i + 1) / kSegments;
		const float c[4][2] = { { cosf(a0) * r0, sinf(a0) * r0 }, { cosf(a1) * r0, sinf(a1) * r0 },
			{ cosf(a1) * r1, sinf(a1) * r1 }, { cosf(a0) * r1, sinf(a0) * r1 } };
		/* The data face catches the light in colours that turn with angle. */
		ImU32 color = IM_COL32(255, 255, 255, 255);
		if (hue) {
			const float h = a0 * 2.0f;
			color = IM_COL32(205 + (int)(30 * sinf(h)), 205 + (int)(30 * sinf(h + 2.1f)),
				215 + (int)(30 * sinf(h + 4.2f)), 255);
		}
		static const int front[6] = { 0, 1, 2, 0, 2, 3 }, back[6] = { 0, 2, 1, 0, 3, 2 };
		const int *order = nz > 0 ? front : back;
		for (int j = 0; j < 6; j++) {
			const int k = order[j];
			push(out, c[k][0], DISC3D_CENTER_Y + c[k][1], z, n,
				0.5f + c[k][0] / (2.0f * DISC3D_RADIUS), 0.5f - c[k][1] / (2.0f * DISC3D_RADIUS), color);
		}
	}
}

void build()
{
	const float front = kThickness * 0.5f, back = -kThickness * 0.5f;
	ring(s_label, kHub, DISC3D_RADIUS, front, 1.0f, false);
	ring(s_hub, kHole, kHub, front, 1.0f, false);
	ring(s_data, kHole, DISC3D_RADIUS, back, -1.0f, true);
	for (int i = 0; i < kSegments; i++) {
		const float a0 = 2.0f * (float)M_PI * i / kSegments, a1 = 2.0f * (float)M_PI * (i + 1) / kSegments;
		const float n0[3] = { cosf(a0), sinf(a0), 0.0f }, n1[3] = { cosf(a1), sinf(a1), 0.0f };
		const float x0 = n0[0] * DISC3D_RADIUS, y0 = n0[1] * DISC3D_RADIUS;
		const float x1 = n1[0] * DISC3D_RADIUS, y1 = n1[1] * DISC3D_RADIUS;
		const ImU32 white = IM_COL32(255, 255, 255, 255);
		push(s_rim, x0, DISC3D_CENTER_Y + y0, back, n0, 0, 0, white);
		push(s_rim, x1, DISC3D_CENTER_Y + y1, back, n1, 0, 0, white);
		push(s_rim, x1, DISC3D_CENTER_Y + y1, front, n1, 0, 0, white);
		push(s_rim, x0, DISC3D_CENTER_Y + y0, back, n0, 0, 0, white);
		push(s_rim, x1, DISC3D_CENTER_Y + y1, front, n1, 0, 0, white);
		push(s_rim, x0, DISC3D_CENTER_Y + y0, front, n0, 0, 0, white);
	}
}

} // namespace

bool disc3d_init(void)
{
	build();
	return true;
}

void disc3d_shutdown(void)
{
	s_label.clear();
	s_hub.clear();
	s_data.clear();
	s_rim.clear();
}

void disc3d_draw(const float model[16], const DiscLook *look)
{
	const unsigned white = ui_gl_white_texture();
	/* Printed label: semi-gloss. Clear hub, silver data face, coloured rim. */
	const UiMaterial label = { IM_COL32(255, 255, 255, 255), 0.30f, 40.0f, 0 };
	const UiMaterial hub = { IM_COL32(190, 196, 210, 255), 0.80f, 70.0f, 0 };
	const UiMaterial data = { IM_COL32(200, 204, 214, 255), 0.95f, 80.0f, 0 };
	const UiMaterial rim = { look->rim, 0.60f, 50.0f, 0 };
	ui_gl3d_draw(s_label.data(), (int)s_label.size(), look->label ? look->label : white, model, look->light, &label);
	ui_gl3d_draw(s_hub.data(), (int)s_hub.size(), white, model, look->light, &hub);
	ui_gl3d_draw(s_data.data(), (int)s_data.size(), white, model, look->light, &data);
	ui_gl3d_draw(s_rim.data(), (int)s_rim.size(), white, model, look->light, &rim);
}
