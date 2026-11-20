/* dock.h: the macOS dock, drawn by the compositor at the bottom of every
 * monitor: liquid glass, the pinned apps (config.h), then running apps that
 * aren't pinned, then after a separator the minimized windows. A dot marks
 * running apps; hovering shows the app's name above it.
 *
 * Click: a minimized window comes back; an app with a window on screen gets
 * its most recent one minimized; one with windows on another tag gets the
 * most recent focused (switching to it), or its minimized one restored;
 * a pinned app without windows is launched. Middle click opens a new one.
 *
 * It hides below the screen, over the windows rather than taking room from
 * them, and slides up when the cursor touches the bottom edge (and while a
 * window minimizes into it). */

#define MAXDOCK 32

typedef struct {
	App *app;
	const DockApp *pin; /* NULL if not pinned */
	Client *min;        /* the minimized window this item stands for */
	int running;
	int x;              /* left edge, logical px from the dock's left */
} DockItem;

typedef struct Dock {
	struct wlr_scene_blur *blur; /* the glass */
	struct wlr_scene_rect *bg;   /* its tint */
	struct wlr_scene_buffer *icons;
	struct wlr_scene_blur *labelblur;
	struct wlr_scene_rect *labelbg;
	struct wlr_scene_buffer *label;
	struct wlr_box box;          /* layout coordinates, when shown */
	DockItem items[MAXDOCK];
	int n, hover;
	int off;                     /* not drawn on this monitor */
	int shown;                   /* up, or sliding up */
	float t;                     /* 0 hidden .. 1 shown */
	int64_t last, hold;          /* last slide step; kept up until hold (ms) */
	int64_t leave;               /* up until then after the cursor left it (ms) */
} Dock;

static void dockplace(Monitor *m);

static int
appmatch(Client *c, const char *id)
{
	const char *a = client_get_appid(c);
	return a && !strcasecmp(a, id);
}

static void
dockitems(Dock *d, int size)
{
	Client *c;
	size_t i;
	int j;

	d->n = 0;
	for (i = 0; i < LENGTH(dockapps) && d->n < MAXDOCK; i++) {
		DockItem *it = &d->items[d->n++];
		*it = (DockItem){ .app = appget(dockapps[i].id, size), .pin = &dockapps[i] };
	}
	/* running apps, pinned or not, in the order they were opened */
	wl_list_for_each(c, &clients, link) {
		const char *id = client_get_appid(c);
		if (client_is_unmanaged(c))
			continue;
		for (j = 0; j < d->n; j++)
			if (!strcasecmp(d->items[j].app->id, id && *id ? id : "unknown"))
				break;
		if (j == d->n && d->n < MAXDOCK)
			d->items[d->n++] = (DockItem){ .app = appget(id, size) };
		if (j < d->n)
			d->items[j].running = 1;
	}
	wl_list_for_each(c, &clients, link)
		if (c->isminimized && d->n < MAXDOCK)
			d->items[d->n++] = (DockItem){ .app = appget(client_get_appid(c), size), .min = c };
}

static void
dockdraw(Monitor *m)
{
	Dock *d = m->dock;
	float s = m->wlr_output->scale;
	int i, x, w, h, size = (int)lroundf((float)dock_icon * s), seen_min = 0;
	Buffer *buf;
	pixman_image_t *icon;

	if (!d)
		return;
	if (!showdock || !m->wlr_output->enabled || mirrorof(m)) {
		d->off = 1;
		wlr_scene_node_set_enabled(&d->blur->node, 0);
		wlr_scene_node_set_enabled(&d->bg->node, 0);
		wlr_scene_node_set_enabled(&d->icons->node, 0);
		return;
	}
	d->off = 0;
	dockitems(d, size);

	/* layout, logical px */
	x = dock_pad;
	for (i = 0; i < d->n; i++) {
		if (d->items[i].min && !seen_min++)
			x += dock_gap + 1; /* room for the separator */
		d->items[i].x = x;
		x += dock_icon + dock_gap;
	}
	w = x - dock_gap + dock_pad;
	h = dock_icon + 2 * dock_pad;
	d->box = (struct wlr_box){ m->m.x + (m->m.width - w) / 2,
		m->m.y + m->m.height - dock_margin - h, w, h };

	buf = newbuf((int)ceilf((float)w * s), (int)ceilf((float)h * s));
	seen_min = 0;
	for (i = 0; i < d->n; i++) {
		DockItem *it = &d->items[i];
		int ix = (int)lroundf((float)it->x * s), iy = (int)lroundf((float)dock_pad * s);
		if (it->min && !seen_min++) {
			float sx = ((float)it->x - (float)dock_gap / 2.0f - 0.5f) * s;
			paintline(buf->data, buf->base.width, buf->base.height,
					sx, (float)h * 0.2f * s, sx, (float)h * 0.8f * s, s, dock_separator);
		}
		icon = pixman_image_create_bits(PIXMAN_a8r8g8b8, size, size, it->app->px, size * 4);
		pixman_image_composite32(PIXMAN_OP_OVER, icon, NULL, buf->image,
				0, 0, 0, 0, ix, iy, size, size);
		pixman_image_unref(icon);
		if (it->running)
			paintcircle(buf->data, buf->base.width, buf->base.height,
					((float)it->x + (float)dock_icon / 2.0f) * s,
					(float)(dock_pad + dock_icon + 3) * s, 1.75f * s, dock_dot);
	}

	wlr_scene_buffer_set_buffer(d->icons, &buf->base);
	wlr_buffer_drop(&buf->base);
	wlr_scene_buffer_set_dest_size(d->icons, w, h);
	wlr_scene_rect_set_size(d->bg, w, h);
	wlr_scene_rect_set_corner_radius(d->bg, dock_radius);
	wlr_scene_blur_set_size(d->blur, w, h);
	wlr_scene_blur_set_corner_radius(d->blur, dock_radius);
	dockplace(m);
}

/* Put the dock t of the way up from just below the screen */
static void
dockplace(Monitor *m)
{
	Dock *d = m->dock;
	float e = d->t * d->t * (3 - 2 * d->t); /* ease in and out */
	int y = d->box.y + (int)lroundf((1 - e) * (float)(d->box.height + dock_margin));
	int on = !d->off && d->t > 0;
	wlr_scene_node_set_position(&d->icons->node, d->box.x, y);
	wlr_scene_node_set_position(&d->bg->node, d->box.x, y);
	wlr_scene_node_set_position(&d->blur->node, d->box.x, y);
	wlr_scene_node_set_enabled(&d->blur->node, on);
	wlr_scene_node_set_enabled(&d->bg->node, on);
	wlr_scene_node_set_enabled(&d->icons->node, on);
}

static void dockupdate(Monitor *m);

static int
dockhidetimer(void *data)
{
	Monitor *m;
	wl_list_for_each(m, &mons, link)
		dockupdate(m);
	return 0;
}

/* Whether the dock on m should be up: the cursor is at the bottom edge or on
 * the shown dock (or left it less than dock_hide_ms ago), or a window is
 * minimizing into it */
static int
dockwant(Monitor *m)
{
	static struct wl_event_source *hide;
	Dock *d = m->dock;
	struct wlr_box b = d->box;
	b.height += dock_margin; /* the gap below it */
	if (xytomon(cursor->x, cursor->y) == m && (cursor->y >= m->m.y + m->m.height - 2
			|| (d->shown && wlr_box_contains_point(&b, cursor->x, cursor->y)))) {
		if (!hide)
			hide = wl_event_loop_add_timer(event_loop, dockhidetimer, NULL);
		d->leave = nowms() + dock_hide_ms;
		wl_event_source_timer_update(hide, dock_hide_ms + 1); /* recheck once the hold ran out */
		return 1;
	}
	return nowms() < d->hold || nowms() < d->leave;
}

/* Start sliding the dock on m if it should come up or go away */
static void
dockupdate(Monitor *m)
{
	Dock *d = m->dock;
	int want;
	if (!d || d->off || (want = dockwant(m)) == d->shown)
		return;
	d->shown = want;
	d->last = nowms();
	wlr_output_schedule_frame(m->wlr_output);
}

/* Keep the dock on m up for ms */
static void
dockhold(Monitor *m, int ms)
{
	if (!m || !m->dock)
		return;
	m->dock->hold = nowms() + ms;
	dockupdate(m);
}

/* Slide the dock on m a frame further; returns 1 while it moves or is held */
static int
dockstep(Monitor *m, int64_t now)
{
	Dock *d = m->dock;
	float step;
	if (!d || d->off)
		return 0;
	d->shown = dockwant(m);
	step = dock_slide_ms ? (float)(now - d->last) / (float)dock_slide_ms : 1;
	d->last = now;
	if (d->t == (float)d->shown)
		return now < d->hold;
	d->t = d->shown ? MIN(1.0f, d->t + step) : MAX(0.0f, d->t - step);
	dockplace(m);
	return 1;
}

static void
docklabel(Monitor *m)
{
	Dock *d = m->dock;
	float s = m->wlr_output->scale;
	uint32_t scheme[3] = { 0xffffffff, 0 };
	const char *name;
	int tw, w, h, x, y;
	Buffer *buf;

	if (d->hover < 0 || d->hover >= d->n) {
		wlr_scene_node_set_enabled(&d->label->node, 0);
		wlr_scene_node_set_enabled(&d->labelbg->node, 0);
		wlr_scene_node_set_enabled(&d->labelblur->node, 0);
		return;
	}
	name = d->items[d->hover].min ? client_get_title(d->items[d->hover].min)
		: d->items[d->hover].app->name;
	if (!name || !*name)
		name = d->items[d->hover].app->name;
	drwl_setfont(m->drw, m->drw->font);
	tw = MIN((int)drwl_font_getwidth(m->drw, name), (int)(400 * s));
	w = (int)((float)tw / s) + 20;
	h = 26;
	x = d->box.x + d->items[d->hover].x + dock_icon / 2 - w / 2;
	x = MAX(m->m.x + 4, MIN(x, m->m.x + m->m.width - w - 4));
	y = d->box.y - h - 8;

	buf = newbuf(tw, (int)ceilf((float)h * s));
	drwl_setimage(m->drw, buf->image);
	drwl_setscheme(m->drw, scheme);
	drwl_text(m->drw, 0, 0, (unsigned)tw, (unsigned)buf->base.height, 0, name, 0);
	drwl_setimage(m->drw, NULL);
	wlr_scene_buffer_set_buffer(d->label, &buf->base);
	wlr_buffer_drop(&buf->base);
	wlr_scene_buffer_set_dest_size(d->label, (int)((float)tw / s), h);
	wlr_scene_node_set_position(&d->label->node, x + 10, y);
	wlr_scene_rect_set_size(d->labelbg, w, h);
	wlr_scene_node_set_position(&d->labelbg->node, x, y);
	wlr_scene_blur_set_size(d->labelblur, w, h);
	wlr_scene_node_set_position(&d->labelblur->node, x, y);
	wlr_scene_node_set_enabled(&d->label->node, 1);
	wlr_scene_node_set_enabled(&d->labelbg->node, 1);
	wlr_scene_node_set_enabled(&d->labelblur->node, 1);
}

static void
dockcreate(Monitor *m)
{
	Dock *d = m->dock = ecalloc(1, sizeof(*d));
	d->hover = -1;
	d->bg = wlr_scene_rect_create(layers[LyrTop], 0, 0, dock_color);
	d->blur = glassbelow(&d->bg->node);
	d->icons = wlr_scene_buffer_create(layers[LyrTop], NULL);
	d->labelbg = wlr_scene_rect_create(layers[LyrTop], 0, 0, dock_color);
	wlr_scene_rect_set_corner_radius(d->labelbg, 8);
	d->labelblur = glassbelow(&d->labelbg->node);
	wlr_scene_blur_set_corner_radius(d->labelblur, 8);
	d->label = wlr_scene_buffer_create(layers[LyrTop], NULL);
	docklabel(m);
}

static void
dockdestroy(Monitor *m)
{
	Dock *d = m->dock;
	if (!d)
		return;
	wlr_scene_node_destroy(&d->blur->node);
	wlr_scene_node_destroy(&d->bg->node);
	wlr_scene_node_destroy(&d->icons->node);
	wlr_scene_node_destroy(&d->labelblur->node);
	wlr_scene_node_destroy(&d->labelbg->node);
	wlr_scene_node_destroy(&d->label->node);
	free(d);
	m->dock = NULL;
}

void
dockchanged(void)
{
	Monitor *m;
	wl_list_for_each(m, &mons, link)
		dockdraw(m);
}

/* The dock item at layout x, y on m, or -1 */
static int
dockat(Monitor *m, double x, double y)
{
	Dock *d = m ? m->dock : NULL;
	int i, lx;
	if (!d || d->off || !d->shown || !wlr_box_contains_point(&d->box, x, y))
		return -1;
	lx = (int)x - d->box.x;
	for (i = 0; i < d->n; i++)
		if (lx >= d->items[i].x - dock_gap / 2 && lx < d->items[i].x + dock_icon + dock_gap / 2)
			return i;
	return -1;
}

static void
dockhover(double x, double y)
{
	Monitor *m, *at = xytomon(x, y);
	wl_list_for_each(m, &mons, link) {
		int i;
		dockupdate(m);
		i = m == at ? dockat(m, x, y) : -1;
		if (m->dock && i != m->dock->hover) {
			m->dock->hover = i;
			docklabel(m);
		}
	}
}

static void
docklaunch(const DockApp *pin)
{
	Arg a = { .v = pin->cmd };
	if (pin && pin->cmd)
		spawn(&a);
}

/* Handle a click at the cursor if it is on a dock; returns 1 if it was */
static int
dockpress(uint32_t button)
{
	Monitor *m = xytomon(cursor->x, cursor->y);
	int i = dockat(m, cursor->x, cursor->y);
	DockItem *it;
	Client *c, *best = NULL, *min = NULL;

	if (!m || !m->dock || m->dock->off || !m->dock->shown
			|| !wlr_box_contains_point(&m->dock->box, cursor->x, cursor->y))
		return 0;
	if (i < 0)
		return 1;
	it = &m->dock->items[i];
	if (it->min) {
		unminimize(it->min);
		return 1;
	}
	if (button == BTN_MIDDLE) {
		if (it->pin)
			docklaunch(it->pin);
		return 1;
	}
	if (button != BTN_LEFT)
		return 1;
	/* the app's most recently focused window, preferring the current tag */
	wl_list_for_each(c, &fstack, flink) {
		if (!appmatch(c, it->app->id) || client_is_unmanaged(c))
			continue;
		if (c->isminimized) {
			if (!min)
				min = c;
			continue;
		}
		if (VISIBLEON(c, c->mon) && c->mon == selmon) {
			best = c;
			break;
		}
		if (!best)
			best = c;
	}
	if (best && VISIBLEON(best, best->mon) && best->mon == selmon) {
		minimize(best); /* on screen: the click toggles it away */
	} else if (best) {
		if (!VISIBLEON(best, best->mon)) {
			Arg a = { .ui = best->tags };
			selmon = best->mon;
			view(&a);
		}
		focusclient(best, 1);
	} else if (min) {
		unminimize(min);
	} else if (it->pin) {
		docklaunch(it->pin);
	}
	return 1;
}
