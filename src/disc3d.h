/* An optical disc for console games, after PCSX AwO's selector: the
 * cover printed on the label face, a black PS1 data face, a sheen that
 * moves as the disc turns. */
#ifndef ARCADE_SHELF_DISC3D_H
#define ARCADE_SHELF_DISC3D_H

typedef struct {
	unsigned label;   /* label texture (square; the disc crops it round) */
	float yaw, pitch; /* the disc's turn, degrees: it moves the sheen */
	float light;      /* 1 selected, less for neighbours */
} DiscLook;

bool disc3d_init(void);
void disc3d_shutdown(void);
/* Draws a disc standing on the carousel spot of model (as a cabinet's);
 * call between ui_gl3d_begin and ui_gl3d_end. */
void disc3d_draw(const float model[16], const DiscLook *look);

/* Size of the generated label texture. */
#define DISC3D_LABEL_SIZE 512
/* The disc's centre height and radius, in cabinet units. */
#define DISC3D_CENTER_Y 1.02f
#define DISC3D_RADIUS 0.62f

#endif
