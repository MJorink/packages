/* Image decoding for the dock icons and the wallpaper: PNG and JPEG through
 * stb_image, SVG through nanosvg. Built as its own object without glasswm's
 * warning flags, since the decoders are third-party code. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO_FAILURE_STRINGS
#include "stb_image.h"

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include <stdio.h>
#include <math.h>
#include "nanosvg.h"
#include "nanosvgrast.h"

/* RGBA bytes (straight alpha) to premultiplied native-endian ARGB, in place */
static void
premultiply(uint32_t *px, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) {
		unsigned char *p = (unsigned char *)&px[i];
		uint32_t r = p[0], g = p[1], b = p[2], a = p[3];
		r = (r * a + 127) / 255;
		g = (g * a + 127) / 255;
		b = (b * a + 127) / 255;
		px[i] = a << 24 | r << 16 | g << 8 | b;
	}
}

static int
endswith(const char *s, const char *suffix)
{
	size_t n = strlen(s), m = strlen(suffix);
	return n >= m && !strcasecmp(s + n - m, suffix);
}

/* Decode path into premultiplied ARGB32 pixels (free() them). SVGs are
 * rasterized to fit svgsize x svgsize; raster images keep their size.
 * Returns NULL if the file can't be read or decoded. */
uint32_t *
img_load(const char *path, int svgsize, int *w, int *h)
{
	uint32_t *px = NULL;

	if (endswith(path, ".svg")) {
		NSVGimage *svg;
		NSVGrasterizer *rast;
		float scale;
		if (!(svg = nsvgParseFromFile(path, "px", 96.0f)))
			return NULL;
		if (svg->width <= 0 || svg->height <= 0 || !(rast = nsvgCreateRasterizer())) {
			nsvgDelete(svg);
			return NULL;
		}
		scale = (float)svgsize / (svg->width > svg->height ? svg->width : svg->height);
		*w = (int)(svg->width * scale + 0.5f);
		*h = (int)(svg->height * scale + 0.5f);
		if (*w > 0 && *h > 0 && (px = calloc((size_t)*w * *h, 4)))
			nsvgRasterize(rast, svg, 0, 0, scale, (unsigned char *)px, *w, *h, *w * 4);
		nsvgDeleteRasterizer(rast);
		nsvgDelete(svg);
	} else {
		int n;
		unsigned char *data = stbi_load(path, w, h, &n, 4);
		if (!data)
			return NULL;
		px = (uint32_t *)data; /* stbi_load uses malloc: free() works */
	}
	if (px)
		premultiply(px, (size_t)*w * *h);
	return px;
}
