/*
 * Device-independent low level convblit routines - 24/32bpp RGB/A in/out (any order) or 16bpp 565/555
 *
 * Copyright (c) 2010 Greg Haerr <greg@censoft.com>
 *
 * This file will need to be modified when adding a new hardware framebuffer
 * image format.  Currently, 32bpp BGRA, 24bpp BGR, and 16bpp RGB 565/555 are defined.
 *
 * These routines do no range checking, clipping, or cursor
 * overwriting checks, but instead draw directly to the
 * data_out memory buffer specified in the passed BLITPARMS struct.
 */
#include "device.h"
#include "convblit.h"

/* for convenience in specifying inline parms*/
#define R		0		/* RGBA parms*/
#define G		1
#define B		2
#define A		3

#define NONE	0		/* portrait parm*/
#define LEFT	1
#define RIGHT	2
#define DOWN	3

#define COPY	0		/* mode parm*/
#define SRCOVER	1

/*
 * Conversion blit for COPY or SRCOVER from RGBA or RGB input to
 * 32 or 24bpp output, and rotate according to portrait specified.
 *
 * The gcc inline mechanism will compile this function with the
 * result of no switch and few if statements, as most use constant comparisons,
 * and will be optimized out.  Thus, the inner loops run very fast!
 * This is also true for the copy vs srcover.  When COPY is specified,
 * no blending code will be included.
 */
static inline void convblit_8888(PSD psd, PMWBLITPARMS gc, int mode,
	int SSZ, int SR, int SG, int SB, int SA,
	int DSZ, int DR, int DG, int DB, int DA, int PORTRAIT)
{
	unsigned char *src, *dst;
	int dsz, dst_pitch;
	int height, newx, newy;
	int src_pitch = gc->src_pitch;

	/* compiler will optimize out switch statement and most else to constants*/
	switch (PORTRAIT) {
	case NONE:
		dsz = DSZ;					/* dst: next pixel over*/
		dst_pitch = gc->dst_pitch;	/* dst: next line down*/
		break;

	case LEFT:
		/* change dst top left to lower left for left portrait*/
		/* rotate left: X -> Y, Y -> maxx - X*/
		newx = gc->dsty;
		gc->dsty = psd->xvirtres - gc->dstx - 1;
		gc->dstx = newx;

		dsz = -gc->dst_pitch;		/* dst: next row up*/
		dst_pitch = DSZ;			/* dst: next pixel right*/
		break;

	case RIGHT:
		/* change dst top left to upper right for right portrait*/
 		/* Rotate right: X -> maxy - y - h, Y -> X, W -> H, H -> W*/
		newy = gc->dstx;
		gc->dstx = psd->yvirtres - gc->dsty - 1;
		gc->dsty = newy;

		dsz = gc->dst_pitch;		/* dst: next pixel down*/
		dst_pitch = -DSZ;			/* dst: next pixel left*/
		break;

	case DOWN:
		/* change dst top left to lower right for down portrait*/
 		/* Rotate down: X -> maxx - x - w, Y -> maxy - y - h*/
		gc->dstx = psd->xvirtres - gc->dstx - 1;
		gc->dsty = psd->yvirtres - gc->dsty - 1;

		dsz = -DSZ;					/* dst: next pixel left*/
		dst_pitch = -gc->dst_pitch;	/* dst: next pixel up*/
		break;
	}

	src = ((unsigned char *)gc->data)     + gc->srcy * gc->src_pitch + gc->srcx * SSZ;
	dst = ((unsigned char *)gc->data_out) + gc->dsty * gc->dst_pitch + gc->dstx * DSZ;

	height = gc->height;
	while (--height >= 0)
	{
		register unsigned char *d = dst;
		register unsigned char *s = src;
		unsigned int alpha;
		int w = gc->width;

		while (--w >= 0)
		{
			/* inline implementation will optimize out all but two compares in inner loop*/
			if (mode == COPY || (alpha = s[SA]) == 255)		/* copy source*/
			{
				if (DSZ == 2)
					((unsigned short *)d)[0] = RGB2PIXEL565(s[SR], s[SG], s[SB]);
				else
				{
					if (DA >= 0)			/* compiler will optimize out completely*/
						d[DA] = (SA >= 0)? s[SA]: 255;

					d[DR] = s[SR];
					d[DG] = s[SG];
					d[DB] = s[SB];
				}
			}
			else if (alpha != 0)							/* blend source w/dest*/
			{
				if (DSZ == 2) {
					unsigned short sr = RED2PIXEL(s[SR]);
					unsigned short sg = GREEN2PIXEL(s[SG]);
					unsigned short sb = BLUE2PIXEL(s[SB]);
					alpha = 255 - alpha + 1; /* flip alpha then add 1 (see muldiv255)*/

					/* d = muldiv255(255-a, d - s) + s*/
					((unsigned short *)d)[0] = muldiv255_16bpp(((unsigned short *)d)[0], sr, sg, sb, alpha);
				}
				else
				{
 					/* d += muldiv255(a, 255 - d)*/
					if (DA >= 0)
						d[DA] += muldiv255(alpha, 255 - d[DA]);

 					/* d += muldiv255(a, s - d)*/
					d[DR] += muldiv255(alpha, s[SR] - d[DR]);
					d[DG] += muldiv255(alpha, s[SG] - d[DG]);
					d[DB] += muldiv255(alpha, s[SB] - d[DB]);
				}
			}
			d += dsz;
			s += SSZ;				/* src: next pixel right*/
		}
		src += src_pitch;			/* src: next line down*/
		dst += dst_pitch;
	}
printf("convblit_8888\n");
	/* update screen bits if driver requires it*/
	if (!psd->Update)
		return;

	switch (PORTRAIT) {		/* switch will be optimized out*/
	case NONE:
		psd->Update(psd, gc->dstx, gc->dsty, gc->width, gc->height);
		break;

	case LEFT:
		/* adjust x,y,w,h to physical top left and w/h*/
		psd->Update(psd, gc->dstx, gc->dsty - gc->width + 1, gc->height, gc->width);
		break;

	case RIGHT:
		/* adjust x,y,w,h to physical top left and w/h*/
		psd->Update(psd, gc->dstx - gc->height + 1, gc->dsty, gc->height, gc->width);
		break;

	case DOWN:
		/* adjust x,y,w,h to physical top left and w/h*/
		psd->Update(psd, gc->dstx - gc->width + 1, gc->dsty - gc->height + 1, gc->width, gc->height);
		break;
	}
}

/*---------- 32bpp BGRA output ----------*/

/* Conversion blit composite src_over 32bpp RGBA image to 32bpp BGRA image*/
void convblit_srcover_rgba8888_bgra8888(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, SRCOVER, 4, R,G,B,A, 4, B,G,R,A, NONE);
}

void convblit_srcover_rgba8888_bgra8888_left(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, SRCOVER, 4, R,G,B,A, 4, B,G,R,A, LEFT);
}

void convblit_srcover_rgba8888_bgra8888_right(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, SRCOVER, 4, R,G,B,A, 4, B,G,R,A, RIGHT);
}

void convblit_srcover_rgba8888_bgra8888_down(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, SRCOVER, 4, R,G,B,A, 4, B,G,R,A, DOWN);
}

/* Conversion blit copy 24bpp RGB image to 32bpp BGRA image*/
void convblit_copy_rgb888_bgra8888(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, COPY, 3, R,G,B,-1, 4, B,G,R,A, NONE);
}

void convblit_copy_rgb888_bgra8888_left(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, COPY, 3, R,G,B,-1, 4, B,G,R,A, LEFT);
}

void convblit_copy_rgb888_bgra8888_right(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, COPY, 3, R,G,B,-1, 4, B,G,R,A, RIGHT);
}

void convblit_copy_rgb888_bgra8888_down(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, COPY, 3, R,G,B,-1, 4, B,G,R,A, DOWN);
}

#if LATER
/* Conversion blit copy 32bpp RGBA image to 32bpp BGRA image*/
void convblit_copy_rgba8888_bgra8888(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, COPY, 4, R,G,B,A, 4, B,G,R,A, NONE);
}
#endif

/*---------- 24bpp BGR output ----------*/

/* Conversion blit composite src_over 32bpp RGBA image to 24bpp BGR image*/
void convblit_srcover_rgba8888_bgr888(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, SRCOVER, 4, R,G,B,A, 3, B,G,R,-1, NONE);
}

void convblit_srcover_rgba8888_bgr888_left(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, SRCOVER, 4, R,G,B,A, 3, B,G,R,-1, LEFT);
}

void convblit_srcover_rgba8888_bgr888_right(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, SRCOVER, 4, R,G,B,A, 3, B,G,R,-1, RIGHT);
}

void convblit_srcover_rgba8888_bgr888_down(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, SRCOVER, 4, R,G,B,A, 3, B,G,R,-1, DOWN);
}

/* Conversion blit copy 24bpp RGB image to 24bpp BGR image*/
void convblit_copy_rgb888_bgr888(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, COPY, 3, R,G,B,-1, 3, B,G,R,-1, NONE);
}

void convblit_copy_rgb888_bgr888_left(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, COPY, 3, R,G,B,-1, 3, B,G,R,-1, LEFT);
}

void convblit_copy_rgb888_bgr888_right(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, COPY, 3, R,G,B,-1, 3, B,G,R,-1, RIGHT);
}

void convblit_copy_rgb888_bgr888_down(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, COPY, 3, R,G,B,-1, 3, B,G,R,-1, DOWN);
}

#if LATER
/* Conversion blit copy 32bpp RGBA image to 24bpp BGR image*/
void convblit_copy_rgba8888_bgr888(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, COPY, 4, R,G,B,A, 3, B,G,R,-1, NONE);
}
#endif

/*---------- 16bpp BGR output ----------*/

/* Conversion blit composite src_over 32bpp RGBA image to 16bpp image*/
void convblit_srcover_rgba8888_16bpp(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, SRCOVER, 4, R,G,B,A, 2, 0,0,0,-1, NONE);
}

void convblit_srcover_rgba8888_16bpp_left(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, SRCOVER, 4, R,G,B,A, 2, 0,0,0,-1, LEFT);
}

void convblit_srcover_rgba8888_16bpp_right(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, SRCOVER, 4, R,G,B,A, 2, 0,0,0,-1, RIGHT);
}

void convblit_srcover_rgba8888_16bpp_down(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, SRCOVER, 4, R,G,B,A, 2, 0,0,0,-1, DOWN);
}

/* Conversion blit copy 24bpp RGB image to 16bpp image*/
void convblit_copy_rgb888_16bpp(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, COPY, 3, R,G,B,-1, 2, 0,0,0,-1, NONE);
}

void convblit_copy_rgb888_16bpp_left(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, COPY, 3, R,G,B,-1, 2, 0,0,0,-1, LEFT);
}

void convblit_copy_rgb888_16bpp_right(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, COPY, 3, R,G,B,-1, 2, 0,0,0,-1, RIGHT);
}

void convblit_copy_rgb888_16bpp_down(PSD psd, PMWBLITPARMS gc)
{
	convblit_8888(psd, gc, COPY, 3, R,G,B,-1, 2, 0,0,0,-1, DOWN);
}