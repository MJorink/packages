/* anim.h: macOS window animations drawn from a window's buffers.
 *
 * scenefx can't transform a scene tree, so an animation copies the window:
 * its buffers (content, subsurfaces, title, traffic lights) and rects (the
 * title bar) become "items", which are drawn every frame into a tree of their
 * own as horizontal strips. Each strip can be moved and squeezed on its own,
 * which is enough for the genie (minimize to the dock and back) and for the
 * zooms of opening and closing windows. The window itself is hidden meanwhile.
 *
 * Items lock their buffers, so a closing window can still be drawn after the
 * client is gone. An opening window's items are taken again every frame, as
 * new windows usually redraw at their final size while they appear. */

enum { AnimOpen, AnimClose, AnimGenie };

#define MAXITEMS 24

typedef struct {
	struct wlr_buffer *buf; /* NULL for a rect */
	struct wlr_fbox src;    /* in buffer pixels */
	struct wlr_box r;       /* window-relative, logical px */
	float color[4];         /* rect colour, premultiplied */
	float opacity;
	struct fx_corner_radii corners;
} AnimItem;

typedef struct Anim {
	Client *c;      /* NULL once the window is gone (closing) */
	Monitor *m;
	int mode, reverse, live;
	int64_t start;
	int ms;
	int w, h;       /* size of the window the items were taken from */
	struct wlr_box win;  /* where the window is drawn when it isn't animating */
	struct wlr_box icon; /* genie: its dock icon */
	AnimItem items[MAXITEMS];
	int n;
	struct wlr_scene_tree *layer, *tree;
	struct wl_list link;
} Anim;

static struct wl_list anims = { &anims, &anims };

static float
clampf(float x, float lo, float hi)
{
	return x < lo ? lo : x > hi ? hi : x;
}

static float
smooth(float e0, float e1, float x)
{
	float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

/* A critically damped spring from 0 to 1 over t in [0, 1]: how macOS moves
 * things, quick to start and settling softly without overshooting */
static float
spring(float t)
{
	float w = 9.0f;
	return t >= 1.0f ? 1.0f : 1.0f - (1.0f + w * t) * expf(-w * t);
}

static void
releaseitems(Anim *a)
{
	int i;
	for (i = 0; i < a->n; i++)
		if (a->items[i].buf)
			wlr_buffer_unlock(a->items[i].buf);
	a->n = 0;
}

/* Collect the buffers and rects under node, x and y being its parent's
 * window-relative position. Shadows, blurs and the border are left out. */
static void
takeitems(Anim *a, Client *c, struct wlr_scene_node *node, int x, int y)
{
	struct wlr_scene_node *child;
	struct wlr_scene_tree *tree;
	AnimItem *it;

	if (!node->enabled || a->n >= MAXITEMS)
		return;
	x += node->x;
	y += node->y;
	switch (node->type) {
	case WLR_SCENE_NODE_TREE:
		tree = wlr_scene_tree_from_node(node);
		wl_list_for_each(child, &tree->children, link)
			takeitems(a, c, child, x, y);
		break;
	case WLR_SCENE_NODE_BUFFER: {
		struct wlr_scene_buffer *sb = wlr_scene_buffer_from_node(node);
		if (!sb->buffer || sb->transform != WL_OUTPUT_TRANSFORM_NORMAL)
			break;
		it = &a->items[a->n++];
		*it = (AnimItem){ .buf = wlr_buffer_lock(sb->buffer), .src = sb->src_box,
			.opacity = sb->opacity, .corners = sb->corners };
		if (wlr_fbox_empty(&it->src))
			it->src = (struct wlr_fbox){ 0, 0, sb->buffer->width, sb->buffer->height };
		it->r = (struct wlr_box){ x, y, sb->dst_width ? sb->dst_width : sb->buffer->width,
			sb->dst_height ? sb->dst_height : sb->buffer->height };
		break;
	}
	case WLR_SCENE_NODE_RECT: {
		struct wlr_scene_rect *rect = wlr_scene_rect_from_node(node);
		if (rect == c->border)
			break;
		it = &a->items[a->n++];
		*it = (AnimItem){ .r = { x, y, rect->width, rect->height }, .opacity = 1.0f,
			.corners = rect->corners };
		memcpy(it->color, rect->color, sizeof(it->color));
		break;
	}
	default:
		break;
	}
}

static void
takewindow(Anim *a, Client *c)
{
	struct wlr_scene_node *child;
	releaseitems(a);
	/* c->scene itself may be hidden: start from its children */
	wl_list_for_each(child, &c->scene->children, link)
		takeitems(a, c, child, 0, 0);
	a->w = c->geom.width;
	a->h = c->geom.height;
}

/* Genie: where window row v (0 top, 1 bottom) is at progress p (0 the
 * window, 1 in the dock): its y and its left and right edges. The bottom
 * bends towards the icon first, then the window pours into it. */
static void
geniemap(Anim *a, float v, float p, float *y, float *l, float *r)
{
	float T = (float)a->win.y, B = (float)(a->win.y + a->win.height);
	float L = (float)a->win.x, R = (float)(a->win.x + a->win.width);
	float it = (float)a->icon.y, ib = (float)(a->icon.y + a->icon.height);
	float bend = smooth(0.0f, 0.45f, p), slide = smooth(0.2f, 1.0f, p);
	float sv = clampf(slide * 1.7f - (1.0f - v) * 0.7f, 0.0f, 1.0f);
	float q, f;

	*y = T + v * (B - T) + sv * ((it + v * (ib - it)) - (T + v * (B - T)));
	q = clampf((*y - T) / MAX(1.0f, ib - T), 0.0f, 1.0f);
	f = bend * smooth(0.0f, 1.0f, q);
	f = MAX(f, sv); /* rows that arrived are as narrow as the icon */
	*l = L + f * ((float)a->icon.x - L);
	*r = R + f * ((float)(a->icon.x + a->icon.width) - R);
}

/* Draw the items for progress p as strips */
static void
animdraw(Anim *a, float p)
{
	int k, i, strips = a->mode == AnimGenie ? 48 : 1;
	float alpha = 1.0f, H = (float)a->h, W = (float)a->w;

	wlr_scene_node_destroy(&a->tree->node);
	a->tree = wlr_scene_tree_create(a->layer);
	if (a->mode != AnimGenie)
		alpha = p; /* zooms fade */

	for (k = 0; k < strips; k++) {
		float v0 = (float)k / (float)strips, v1 = (float)(k + 1) / (float)strips;
		float y0, y1, l, r, l1, r1;
		if (a->mode == AnimGenie) {
			geniemap(a, v0, p, &y0, &l, &r);
			geniemap(a, v1, p, &y1, &l1, &r1);
			l = (l + l1) / 2;
			r = (r + r1) / 2;
		} else {
			/* zoom about the centre from 85% (opening) or to 92% (closing) */
			float s = a->mode == AnimOpen ? 0.85f + 0.15f * p : 0.92f + 0.08f * p;
			float cx = (float)a->win.x + (float)a->win.width / 2;
			float cy = (float)a->win.y + (float)a->win.height / 2;
			y0 = cy - (float)a->win.height / 2 * s;
			y1 = cy + (float)a->win.height / 2 * s;
			l = cx - (float)a->win.width / 2 * s;
			r = cx + (float)a->win.width / 2 * s;
		}
		for (i = 0; i < a->n; i++) {
			AnimItem *it = &a->items[i];
			float ia = MAX((float)it->r.y, v0 * H), ib = MIN((float)(it->r.y + it->r.height), v1 * H);
			float sx = (r - l) / W, ya, yb;
			int X0, X1, Y0, Y1, top, bottom;
			struct fx_corner_radii cr;
			if (ib <= ia || it->r.width <= 0)
				continue;
			ya = y0 + (ia - v0 * H) / ((v1 - v0) * H) * (y1 - y0);
			yb = y0 + (ib - v0 * H) / ((v1 - v0) * H) * (y1 - y0);
			X0 = (int)lroundf(l + (float)it->r.x * sx);
			X1 = (int)lroundf(l + (float)(it->r.x + it->r.width) * sx);
			Y0 = (int)lroundf(ya);
			Y1 = (int)lroundf(yb);
			if (X1 <= X0 || Y1 <= Y0)
				continue;
			top = ia == (float)it->r.y;
			bottom = ib == (float)(it->r.y + it->r.height);
			cr = corner_radii_new(top ? (int)((float)it->corners.top_left * sx) : 0,
					top ? (int)((float)it->corners.top_right * sx) : 0,
					bottom ? (int)((float)it->corners.bottom_right * sx) : 0,
					bottom ? (int)((float)it->corners.bottom_left * sx) : 0);
			if (it->buf) {
				struct wlr_scene_buffer *sb = wlr_scene_buffer_create(a->tree, it->buf);
				struct wlr_fbox src = it->src;
				src.y = it->src.y + (double)(ia - (float)it->r.y) / it->r.height * it->src.height;
				src.height = (double)(ib - ia) / it->r.height * it->src.height;
				wlr_scene_buffer_set_source_box(sb, &src);
				wlr_scene_buffer_set_dest_size(sb, X1 - X0, Y1 - Y0);
				wlr_scene_buffer_set_opacity(sb, it->opacity * alpha);
				wlr_scene_buffer_set_corner_radii(sb, cr);
				wlr_scene_node_set_position(&sb->node, X0, Y0);
			} else {
				float col[4] = { it->color[0] * alpha, it->color[1] * alpha,
					it->color[2] * alpha, it->color[3] * alpha };
				struct wlr_scene_rect *rect = wlr_scene_rect_create(a->tree, X1 - X0, Y1 - Y0, col);
				wlr_scene_rect_set_corner_radii(rect, cr);
				wlr_scene_node_set_position(&rect->node, X0, Y0);
			}
		}
	}
}

/* Show c as it should be now that no animation hides it */
static void
showclient(Client *c)
{
	if (c->scene && c->mon)
		wlr_scene_node_set_enabled(&c->scene->node,
				(VISIBLEON(c, c->mon) || c->slideout) && !c->anim);
}

static void
animend(Anim *a)
{
	Client *c = a->c;
	wl_list_remove(&a->link);
	wlr_scene_node_destroy(&a->tree->node);
	releaseitems(a);
	if (c) {
		c->anim = NULL;
		showclient(c);
		/* a minimized window kept its place until now: close the gap */
		if (a->mode == AnimGenie && !a->reverse && c->isminimized && c->mon)
			arrange(c->mon);
		if (c->mon)
			wlr_output_schedule_frame(c->mon->wlr_output);
	}
	free(a);
}

/* Start an animation of c (hidden meanwhile) in layer */
static Anim *
animnew(Client *c, int mode, int ms, struct wlr_scene_tree *layer)
{
	Anim *a;
	if (c->anim)
		animend(c->anim);
	a = ecalloc(1, sizeof(*a));
	a->c = c;
	a->m = c->mon;
	a->mode = mode;
	a->ms = ms;
	a->start = nowms();
	a->layer = layer;
	a->tree = wlr_scene_tree_create(layer);
	a->win = (struct wlr_box){ c->scene->node.x, c->scene->node.y, c->geom.width, c->geom.height };
	takewindow(a, c);
	c->anim = a;
	showclient(c);
	wl_list_insert(&anims, &a->link);
	if (a->m)
		wlr_output_schedule_frame(a->m->wlr_output);
	return a;
}

/* Step the animations on m; returns 1 while any runs */
static int
animstep(Monitor *m, int64_t now)
{
	Anim *a, *tmp;
	int running = 0;
	wl_list_for_each_safe(a, tmp, &anims, link) {
		float t, p;
		if (a->m != m)
			continue;
		t = clampf((float)(now - a->start) / (float)a->ms, 0.0f, 1.0f);
		if (t >= 1.0f) {
			animend(a);
			continue;
		}
		if (a->live && a->c) {
			takewindow(a, a->c);
			a->win.width = a->c->geom.width;
			a->win.height = a->c->geom.height;
			a->win.x = a->c->geom.x;
			a->win.y = a->c->geom.y;
		}
		if (a->mode == AnimGenie) {
			/* ease in and out, as the genie accelerates into the dock */
			p = t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
			if (a->reverse)
				p = 1 - p;
		} else if (a->mode == AnimOpen) {
			p = spring(t);
		} else {
			p = 1 - t * t; /* closing: ease in */
		}
		animdraw(a, p);
		running = 1;
	}
	return running;
}

/* A window opens: zoom it in from the middle (from resize(), at its first place) */
static void
animopen(Client *c)
{
	Anim *a;
	if (!animation_ms || c->isfullscreen || !c->mon)
		return;
	a = animnew(c, AnimOpen, open_ms, c->scene->node.parent);
	a->live = 1;
	a->win = c->geom;
	wlr_scene_node_raise_to_top(&a->tree->node);
	animdraw(a, 0);
}

/* A window closes (from unmapnotify(), before its scene is destroyed) */
static void
animclose(Client *c)
{
	Anim *a;
	if (!animation_ms || !c->mon || !c->scene || !c->scene->node.enabled || c->isfullscreen)
		return;
	a = animnew(c, AnimClose, close_ms, c->scene->node.parent);
	a->reverse = 1;
	a->c = NULL; /* the client goes away now */
	c->anim = NULL;
	animdraw(a, 1);
}

/* c's icon in the dock of its monitor; the bottom middle if it has none */
static struct wlr_box
docktarget(Client *c)
{
	Monitor *m = c->mon;
	Dock *d = m ? m->dock : NULL;
	int i;
	if (d && d->icons->node.enabled)
		for (i = 0; i < d->n; i++)
			if (d->items[i].min == c)
				return (struct wlr_box){ d->box.x + d->items[i].x, d->box.y + dock_pad,
					dock_icon, dock_icon };
	if (!m)
		return (struct wlr_box){0};
	return (struct wlr_box){ m->m.x + m->m.width / 2 - dock_icon / 2,
		m->m.y + m->m.height - dock_icon, dock_icon, dock_icon };
}

static void
minimize(Client *c)
{
	Anim *a = NULL;
	if (!c || c->isminimized)
		return;
	if (genie_ms && c->scene->node.enabled && !c->isfullscreen)
		a = animnew(c, AnimGenie, genie_ms, layers[LyrTop]);
	c->isminimized = 1;
	if (c == hoverc)
		hoverc = NULL;
	focusclient(focustop(c->mon), 1);
	arrange(c->mon);
	dockchanged();
	if (a) {
		a->icon = docktarget(c);
		animdraw(a, 0);
	}
}

static void
unminimize(Client *c)
{
	Anim *a = NULL;
	struct wlr_box icon;
	if (!c || !c->isminimized)
		return;
	icon = docktarget(c);
	if (genie_ms) {
		a = animnew(c, AnimGenie, genie_ms, layers[LyrTop]);
		a->reverse = 1;
		a->icon = icon;
	}
	c->isminimized = 0;
	c->placed = 1; /* no zoom: the genie brings it back */
	focusclient(c, 1);
	arrange(c->mon);
	dockchanged();
	if (a) {
		a->win = c->geom; /* where the layout puts it now */
		animdraw(a, 1);
	}
}
