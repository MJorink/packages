/* deco.h: macOS title bars, drawn by the compositor for every window that
 * asks for server-side decorations (xdg-decoration). Clients that draw
 * their own (GTK) keep theirs and get no title bar.
 *
 * A title bar is a translucent rect over the window's glass (blur), a 1px
 * separator, the title (a buffer, redrawn when it changes) and the traffic
 * lights: one buffer shared by all windows, in three variants (focused,
 * unfocused, hovered with glyphs), drawn antialiased in software. */

enum { DecoNone, DecoTitle, DecoClose, DecoMinimize, DecoZoom };
enum { BtnFocused, BtnUnfocused, BtnHover, BtnLast };

/* traffic lights, logical px: diameter, distance between centres and from
 * the window's left edge to the first one's edge */
#define BTN_D   12
#define BTN_S   20
#define BTN_PAD 13

static struct wlr_buffer *btnbuf[BtnLast];
static float btnscale;
static Drwl *tdrw;         /* title text */
static float tdrwscale;
static Client *hoverc;     /* window whose traffic lights are hovered */
static Client *pressc;     /* window and button pressed, acted on at release */
static int presspart;
static uint32_t lastclick; /* time and window of the last title bar click */
static Client *lastclickc;

/* A software buffer for a scene buffer node. Its pixels are premultiplied
 * ARGB in buf->data; pixman draws into buf->image. */
static Buffer *
newbuf(int w, int h)
{
	Buffer *buf = ecalloc(1, sizeof(Buffer) + (size_t)w * 4 * h);
	buf->image = drwl_image_create(NULL, w, h, buf->data);
	wlr_buffer_init(&buf->base, &buffer_impl, w, h);
	return buf;
}

/* Antialiased shapes: px is w x h premultiplied ARGB, shapes are composited
 * over it with 4x4 supersampling. col is straight 0xAARRGGBB. */
typedef int (*Inside)(const float *s, float x, float y);

static void
paint(uint32_t *px, int w, int h, float x0, float y0, float x1, float y1,
		Inside in, const float *shape, uint32_t col)
{
	int x, y, i, j, n;
	float ca = (float)(col >> 24) / 255.0f;
	float cr = (float)(col >> 16 & 0xff), cg = (float)(col >> 8 & 0xff), cb = (float)(col & 0xff);
	for (y = MAX(0, (int)y0); y < MIN(h, (int)ceilf(y1)); y++) {
		for (x = MAX(0, (int)x0); x < MIN(w, (int)ceilf(x1)); x++) {
			uint32_t d = px[y * w + x];
			float a, ia;
			for (n = 0, j = 0; j < 4; j++)
				for (i = 0; i < 4; i++)
					n += in(shape, (float)x + ((float)i + 0.5f) / 4.0f,
							(float)y + ((float)j + 0.5f) / 4.0f);
			if (!n)
				continue;
			a = ca * (float)n / 16.0f;
			ia = 1.0f - a;
			px[y * w + x] =
				(uint32_t)(a * 255.0f + ia * (float)(d >> 24) + 0.5f) << 24 |
				(uint32_t)(a * cr + ia * (float)(d >> 16 & 0xff) + 0.5f) << 16 |
				(uint32_t)(a * cg + ia * (float)(d >> 8 & 0xff) + 0.5f) << 8 |
				(uint32_t)(a * cb + ia * (float)(d & 0xff) + 0.5f);
		}
	}
}

static int
incircle(const float *s, float x, float y)
{
	return (x - s[0]) * (x - s[0]) + (y - s[1]) * (y - s[1]) <= s[2] * s[2];
}

static void
paintcircle(uint32_t *px, int w, int h, float cx, float cy, float r, uint32_t col)
{
	float s[3] = { cx, cy, r };
	paint(px, w, h, cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1, incircle, s, col);
}

/* segment (s0,s1)-(s2,s3) with half thickness s4 */
static int
inline_(const float *s, float x, float y)
{
	float dx = s[2] - s[0], dy = s[3] - s[1];
	float t = ((x - s[0]) * dx + (y - s[1]) * dy) / (dx * dx + dy * dy);
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	dx = s[0] + t * dx - x;
	dy = s[1] + t * dy - y;
	return dx * dx + dy * dy <= s[4] * s[4];
}

static void
paintline(uint32_t *px, int w, int h, float ax, float ay, float bx, float by,
		float thick, uint32_t col)
{
	float s[5] = { ax, ay, bx, by, thick / 2 };
	paint(px, w, h, MIN(ax, bx) - thick, MIN(ay, by) - thick,
			MAX(ax, bx) + thick, MAX(ay, by) + thick, inline_, s, col);
}

/* triangle (s0,s1) (s2,s3) (s4,s5) */
static int
intri(const float *s, float x, float y)
{
	float d1 = (x - s[2]) * (s[1] - s[3]) - (s[0] - s[2]) * (y - s[3]);
	float d2 = (x - s[4]) * (s[3] - s[5]) - (s[2] - s[4]) * (y - s[5]);
	float d3 = (x - s[0]) * (s[5] - s[1]) - (s[4] - s[0]) * (y - s[1]);
	return !(((d1 < 0) || (d2 < 0) || (d3 < 0)) && ((d1 > 0) || (d2 > 0) || (d3 > 0)));
}

static void
painttri(uint32_t *px, int w, int h, const float t[6], uint32_t col)
{
	paint(px, w, h, MIN(t[0], MIN(t[2], t[4])) - 1, MIN(t[1], MIN(t[3], t[5])) - 1,
			MAX(t[0], MAX(t[2], t[4])) + 1, MAX(t[1], MAX(t[3], t[5])) + 1, intri, t, col);
}

/* rounded rect (s0,s1) size (s2,s3) radius s4 */
static int
inrrect(const float *s, float x, float y)
{
	float r = s[4], cx, cy;
	if (x < s[0] || y < s[1] || x > s[0] + s[2] || y > s[1] + s[3])
		return 0;
	cx = x < s[0] + r ? s[0] + r : x > s[0] + s[2] - r ? s[0] + s[2] - r : x;
	cy = y < s[1] + r ? s[1] + r : y > s[1] + s[3] - r ? s[1] + s[3] - r : y;
	return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}

static void
paintrrect(uint32_t *px, int w, int h, float x, float y, float rw, float rh,
		float r, uint32_t col)
{
	float s[5] = { x, y, rw, rh, r };
	paint(px, w, h, x - 1, y - 1, x + rw + 1, y + rh + 1, inrrect, s, col);
}

/* The traffic lights at scale s, in variant v */
static struct wlr_buffer *
drawbuttons(float s, int v)
{
	/* macOS colours: fill, rim; glyph colours when hovered */
	static const uint32_t fill[3] = { 0xffff5f57, 0xfffebc2e, 0xff28c840 };
	static const uint32_t rim[3]  = { 0xffe2463f, 0xffe1a116, 0xff1aab29 };
	static const uint32_t glyph[3] = { 0xff4d0000, 0xff995700, 0xff006500 };
	int w = (int)ceilf((2 * BTN_S + BTN_D + 2) * s), h = (int)ceilf((BTN_D + 2) * s);
	Buffer *buf = newbuf(w, h);
	float r = BTN_D / 2.0f * s, cy = (float)h / 2.0f, g = 3.0f * s, lw = 1.1f * s;
	int i;

	for (i = 0; i < 3; i++) {
		float cx = (1 + BTN_D / 2.0f + (float)(i * BTN_S)) * s;
		if (v == BtnUnfocused) {
			paintcircle(buf->data, w, h, cx, cy, r, 0xff4e4e50);
			paintcircle(buf->data, w, h, cx, cy, r - 0.5f * s, 0xff464648);
			continue;
		}
		paintcircle(buf->data, w, h, cx, cy, r, rim[i]);
		paintcircle(buf->data, w, h, cx, cy, r - 0.5f * s, fill[i]);
		if (v != BtnHover)
			continue;
		if (i == 0) { /* close: x */
			paintline(buf->data, w, h, cx - g, cy - g, cx + g, cy + g, lw, glyph[i]);
			paintline(buf->data, w, h, cx - g, cy + g, cx + g, cy - g, lw, glyph[i]);
		} else if (i == 1) { /* minimize: - */
			paintline(buf->data, w, h, cx - g - 0.5f * s, cy, cx + g + 0.5f * s, cy,
					lw * 1.1f, glyph[i]);
		} else { /* zoom: two arrow heads */
			float a = 3.0f * s, b = 0.5f * s;
			float t1[6] = { cx - a, cy - a, cx + b, cy - a, cx - a, cy + b };
			float t2[6] = { cx + a, cy + a, cx - b, cy + a, cx + a, cy - b };
			painttri(buf->data, w, h, t1, glyph[i]);
			painttri(buf->data, w, h, t2, glyph[i]);
		}
	}
	return &buf->base;
}

static void
loadtitlefont(float scale)
{
	char attrs[16];
	if (tdrw && tdrwscale == scale)
		return;
	if (!tdrw && !(tdrw = drwl_create()))
		die("failed to create drwl context");
	if (tdrw->font)
		drwl_font_destroy(tdrw->font);
	snprintf(attrs, sizeof(attrs), "dpi=%.2f", 96. * scale);
	if (!drwl_font_create(tdrw, LENGTH(titlefonts), titlefonts, attrs))
		die("Could not load title font");
	tdrwscale = scale;
}

static void
loadbuttons(float scale)
{
	int i;
	if (btnbuf[0] && btnscale == scale)
		return;
	for (i = 0; i < BtnLast; i++) {
		if (btnbuf[i])
			wlr_buffer_drop(btnbuf[i]);
		btnbuf[i] = drawbuttons(scale, i);
	}
	btnscale = scale;
}

/* Whether c gets a title bar: it asked for server-side decorations */
static int
decowanted(Client *c)
{
	return c->decoration && !c->isfullscreen && !client_is_unmanaged(c);
}

/* Create the title bar nodes of c (in mapnotify(), before its first resize) */
static void
decocreate(Client *c)
{
	c->th = decowanted(c) ? titlebar_height : 0;
	c->tbar = wlr_scene_rect_create(c->scene, 0, 0, titlebar_color[1]);
	c->tsep = wlr_scene_rect_create(c->scene, 0, 0, titlebar_separator);
	c->title = wlr_scene_buffer_create(c->scene, NULL);
	c->buttons = wlr_scene_buffer_create(c->scene, NULL);
	c->tbar->node.data = c->tsep->node.data = c;
	c->titlew = -1;
}

/* Redraw c's title for its width, focus and scale */
static void
drawtitle(Client *c)
{
	float s = c->mon ? c->mon->wlr_output->scale : 1.0f;
	int w = (int)ceilf((float)CONTENTW(c) * s), h = (int)ceilf((float)c->th * s);
	/* the title is centred on the window and stays clear of the buttons */
	int side = (int)((BTN_PAD + 2 * BTN_S + BTN_D + 12) * s);
	int tw, avail = w - 2 * side;
	uint32_t scheme[3] = { titlebar_text[c == focused_client], 0 };
	const char *title = client_get_title(c);
	Buffer *buf;

	if (!title || !*title)
		title = client_get_appid(c);
	if (avail <= 0 || h <= 0 || !title) {
		wlr_scene_buffer_set_buffer(c->title, NULL);
		return;
	}
	loadtitlefont(s);
	tw = MIN((int)drwl_font_getwidth(tdrw, title), avail);
	buf = newbuf(tw, h);
	drwl_setimage(tdrw, buf->image);
	drwl_setscheme(tdrw, scheme);
	drwl_text(tdrw, 0, 0, (unsigned)tw, (unsigned)h, 0, title, 0);
	drwl_setimage(tdrw, NULL);

	wlr_scene_buffer_set_buffer(c->title, &buf->base);
	wlr_buffer_drop(&buf->base);
	wlr_scene_buffer_set_dest_size(c->title, (int)((float)tw / s), c->th);
	wlr_scene_node_set_position(&c->title->node,
			CONTENTX(c) + (CONTENTW(c) - (int)((float)tw / s)) / 2, (int)c->bw);
	c->titlew = CONTENTW(c);
}

/* Place and colour the title bar after c's geometry, focus or title changed */
static void
decoupdate(Client *c)
{
	int on = c->th > 0, radius = cornerradius(c), focused = c == focused_client;
	float s = c->mon ? c->mon->wlr_output->scale : 1.0f;

	wlr_scene_node_set_enabled(&c->tbar->node, on);
	wlr_scene_node_set_enabled(&c->tsep->node, on);
	wlr_scene_node_set_enabled(&c->title->node, on);
	wlr_scene_node_set_enabled(&c->buttons->node, on);
	if (!on)
		return;

	wlr_scene_node_set_position(&c->tbar->node, CONTENTX(c), (int)c->bw);
	wlr_scene_rect_set_size(c->tbar, CONTENTW(c), c->th);
	wlr_scene_rect_set_corner_radii(c->tbar, corner_radii_new(radius, radius, 0, 0));
	wlr_scene_rect_set_color(c->tbar, titlebar_color[focused]);
	wlr_scene_node_set_position(&c->tsep->node, CONTENTX(c), CONTENTY(c) - 1);
	wlr_scene_rect_set_size(c->tsep, CONTENTW(c), 1);

	loadbuttons(s);
	wlr_scene_buffer_set_buffer(c->buttons,
			btnbuf[c == hoverc ? BtnHover : focused ? BtnFocused : BtnUnfocused]);
	wlr_scene_buffer_set_dest_size(c->buttons, 2 * BTN_S + BTN_D + 2, BTN_D + 2);
	wlr_scene_node_set_position(&c->buttons->node, CONTENTX(c) + BTN_PAD - 1,
			(int)c->bw + (c->th - BTN_D) / 2 - 1);

	drawtitle(c);
}

/* Give c a title bar or take it away (decorations created or destroyed) */
static void
decoset(Client *c)
{
	int th = decowanted(c) ? titlebar_height : 0;
	if (!c->tbar || th == c->th)
		return;
	c->geom.height += th - c->th;
	c->th = th;
	resize(c, c->geom, 0);
	if (c->mon)
		arrange(c->mon);
}

/* The window whose title bar is at layout x, y and which part of it;
 * NULL if a window's content or anything else is on top there */
static Client *
decoat(double x, double y, int *part)
{
	struct wlr_scene_node *node, *n;
	Client *c = NULL;
	int layer, lx, ly;
	double nx, ny;

	*part = DecoNone;
	for (layer = LyrFS; layer >= LyrTile && !c; layer--) {
		if (!(node = wlr_scene_node_at(&layers[layer]->node, x, y, &nx, &ny)))
			continue;
		for (n = node; n; n = n->parent ? &n->parent->node : NULL)
			if (n->data && ((Client *)n->data)->type == XDGShell) {
				c = n->data;
				break;
			}
		if (!c)
			return NULL; /* a layer surface or popup */
		if (n != &c->scene->node && n != &c->tbar->node && n != &c->tsep->node)
			return NULL; /* its content */
	}
	if (!c || !c->th || !c->tbar->node.enabled)
		return NULL;
	lx = (int)floor(x) - c->geom.x - CONTENTX(c);
	ly = (int)floor(y) - c->geom.y - (int)c->bw;
	if (lx < 0 || ly < 0 || lx >= CONTENTW(c) || ly >= c->th)
		return NULL;
	*part = DecoTitle;
	/* the buttons react a little outside their circles, as on macOS */
	if (ly >= (c->th - BTN_D) / 2 - 3 && ly < (c->th + BTN_D) / 2 + 3
			&& lx >= BTN_PAD - 3 && lx < BTN_PAD + 2 * BTN_S + BTN_D + 3) {
		int i = (lx - BTN_PAD + (BTN_S - BTN_D) / 2) / BTN_S;
		*part = DecoClose + MIN(i, 2);
	}
	return c;
}

/* Hovering any traffic light shows the glyphs of all three */
static void
decohover(Client *c, int part)
{
	Client *old = hoverc;
	hoverc = part >= DecoClose ? c : NULL;
	if (old == hoverc)
		return;
	if (old && old->tbar)
		decoupdate(old);
	if (hoverc)
		decoupdate(hoverc);
}

/* Toggle filling the window area, for c (togglemaximize() acts on the focused window) */
static void
zoomclient(Client *c)
{
	focusclient(c, 1);
	togglemaximize(NULL);
}

/* Left button on a title bar: buttons act on release over the same button;
 * the title focuses, double-click zooms, dragging moves (floating) or swaps
 * (tiled). Returns 1 if the press was on a title bar. */
static int
decopress(int pressed, uint32_t time)
{
	int part;
	Client *c = decoat(cursor->x, cursor->y, &part);

	if (!pressed) {
		if (!pressc)
			return 0;
		if (c == pressc && part == presspart) {
			if (part == DecoClose)
				client_send_close(c);
			else if (part == DecoMinimize)
				minimize(c);
			else if (part == DecoZoom)
				zoomclient(c);
		}
		pressc = NULL;
		return 1;
	}
	if (!c)
		return 0;
	if (part >= DecoClose) {
		pressc = c;
		presspart = part;
		return 1;
	}
	focusclient(c, 1);
	if (lastclickc == c && time - lastclick < 400) {
		lastclickc = NULL;
		zoomclient(c);
		return 1;
	}
	lastclickc = c;
	lastclick = time;
	grabclient(c, CurMove);
	return 1;
}
