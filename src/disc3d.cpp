/* The disc of PCSX AwO's ROM selector by Rinnegatamante, adapted with his
 * permission: its mesh (a printed label face over a black PS1 data face,
 * an inner layer step, the edge and the hole wall) and, in gl.cpp, its
 * shading (UI3D_DISC). */
#include "disc3d.h"

#include <math.h>
#include <vector>

#include <imgui.h>

#include "gl.h"

namespace {

std::vector<UiVertex3D> s_label, s_body;

const int kSegments = 64;
/* In disc radii: the hole, the inner layer edge and the face depths. */
const float kInner = 0.126f, kBackSplit = 0.30f;
const float kTop = 0.009f, kBottom = -0.009f, kInnerBack = -0.0055f;

void push(std::vector<UiVertex3D> &out, float x, float y, float z, const float n[3], float u, float v, ImU32 color)
{
	const float r = DISC3D_RADIUS;
	UiVertex3D vertex = { x * r, DISC3D_CENTER_Y + y * r, z * r, n[0], n[1], n[2], u, v, color };
	out.push_back(vertex);
}

/* A label vertex: the cover maps onto the face, its top at the disc's top. */
void label(float x, float y)
{
	static const float n[3] = { 0.0f, 0.0f, 1.0f };
	push(s_label, x, y, kTop, n, x * 0.5f + 0.5f, 0.5f - y * 0.5f, IM_COL32(255, 255, 255, 255));
}

/* A body vertex: grey shade, and in alpha the part the shader draws. */
void body(float x, float y, float z, const float n[3], float shade, float part)
{
	push(s_body, x, y, z, n, 0.0f, 0.0f,
		IM_COL32((int)(255 * 0.58f * shade), (int)(255 * 0.61f * shade), (int)(255 * 0.66f * shade), (int)(255 * part)));
}

void build()
{
	static const float back[3] = { 0.0f, 0.0f, -1.0f };
	for (int i = 0; i < kSegments; i++) {
		const float a0 = (float)i * 2.0f * (float)M_PI / kSegments, a1 = (float)(i + 1) * 2.0f * (float)M_PI / kSegments;
		const float c0 = cosf(a0), s0 = sinf(a0), c1 = cosf(a1), s1 = sinf(a1);
		const float out0[3] = { c0, s0, 0.0f }, out1[3] = { c1, s1, 0.0f };
		const float in0[3] = { -c0, -s0, 0.0f }, in1[3] = { -c1, -s1, 0.0f };

		label(c0, s0); label(c1, s1); label(c1 * kInner, s1 * kInner);
		label(c0, s0); label(c1 * kInner, s1 * kInner); label(c0 * kInner, s0 * kInner);

		/* Data face, outer part. */
		const float underside = 0.78f + 0.08f * sinf(a0 * 3.0f);
		body(c0, s0, kBottom, back, underside, 0.0f);
		body(c1 * kBackSplit, s1 * kBackSplit, kBottom, back, underside, 0.0f);
		body(c1, s1, kBottom, back, underside, 0.0f);
		body(c0, s0, kBottom, back, underside, 0.0f);
		body(c0 * kBackSplit, s0 * kBackSplit, kBottom, back, underside, 0.0f);
		body(c1 * kBackSplit, s1 * kBackSplit, kBottom, back, underside, 0.0f);
		/* Step up to the inner layer. */
		body(c0 * kBackSplit, s0 * kBackSplit, kBottom, in0, 0.42f, 0.25f);
		body(c1 * kBackSplit, s1 * kBackSplit, kInnerBack, in1, 0.42f, 0.25f);
		body(c1 * kBackSplit, s1 * kBackSplit, kBottom, in1, 0.42f, 0.25f);
		body(c0 * kBackSplit, s0 * kBackSplit, kBottom, in0, 0.42f, 0.25f);
		body(c0 * kBackSplit, s0 * kBackSplit, kInnerBack, in0, 0.42f, 0.25f);
		body(c1 * kBackSplit, s1 * kBackSplit, kInnerBack, in1, 0.42f, 0.25f);
		/* Inner layer. */
		body(c0 * kBackSplit, s0 * kBackSplit, kInnerBack, back, 0.70f, 0.25f);
		body(c1 * kInner, s1 * kInner, kInnerBack, back, 0.70f, 0.25f);
		body(c1 * kBackSplit, s1 * kBackSplit, kInnerBack, back, 0.70f, 0.25f);
		body(c0 * kBackSplit, s0 * kBackSplit, kInnerBack, back, 0.70f, 0.25f);
		body(c0 * kInner, s0 * kInner, kInnerBack, back, 0.70f, 0.25f);
		body(c1 * kInner, s1 * kInner, kInnerBack, back, 0.70f, 0.25f);
		/* Edge. */
		const float edge = 0.48f + 0.08f * c0;
		body(c0, s0, kBottom, out0, edge, 1.0f);
		body(c1, s1, kBottom, out1, edge, 1.0f);
		body(c1, s1, kTop, out1, edge, 1.0f);
		body(c0, s0, kBottom, out0, edge, 1.0f);
		body(c1, s1, kTop, out1, edge, 1.0f);
		body(c0, s0, kTop, out0, edge, 1.0f);
		/* Hole wall. */
		body(c0 * kInner, s0 * kInner, kInnerBack, in0, 0.32f, 1.0f);
		body(c1 * kInner, s1 * kInner, kTop, in1, 0.32f, 1.0f);
		body(c1 * kInner, s1 * kInner, kInnerBack, in1, 0.32f, 1.0f);
		body(c0 * kInner, s0 * kInner, kInnerBack, in0, 0.32f, 1.0f);
		body(c0 * kInner, s0 * kInner, kTop, in0, 0.32f, 1.0f);
		body(c1 * kInner, s1 * kInner, kTop, in1, 0.32f, 1.0f);
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
	s_body.clear();
}

void disc3d_draw(const float model[16], const DiscLook *look)
{
	const unsigned white = ui_gl_white_texture();
	UiMaterial material = {};
	material.tint = IM_COL32(255, 255, 255, 255);
	material.disc_center[1] = DISC3D_CENTER_Y;
	material.disc_radius = DISC3D_RADIUS;
	material.sin_yaw = sinf(look->yaw * (float)M_PI / 180.0f);
	material.sin_pitch = sinf(look->pitch * (float)M_PI / 180.0f);
	/* The body, then the label over it, blended with the cover's alpha. */
	material.flags = UI3D_DISC;
	material.disc_part = 0.0f;
	ui_gl3d_draw(s_body.data(), (int)s_body.size(), white, model, look->light, &material);
	material.flags = UI3D_DISC | UI3D_BLEND;
	material.disc_part = look->label ? 1.0f : 2.0f;
	ui_gl3d_draw(s_label.data(), (int)s_label.size(), look->label ? look->label : white, model, look->light, &material);
}
