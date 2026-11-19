/*
 * glasswm: a light macOS-style Wayland compositor. It started as dwl 0.9
 * with patches (fair layout, scenefx, liquid glass, ...) and adds title
 * bars with traffic lights (deco.h), a dock (dock.h, icons.h) and a menu bar.
 * See LICENSE file for copyright and license details.
 */
#include <fcntl.h>
#include <getopt.h>
#include <libinput.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <scenefx/render/fx_renderer/fx_renderer.h>
#include <scenefx/types/fx/clipped_region.h>
#include <scenefx/types/wlr_scene.h>
#include <libdrm/drm_fourcc.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/libinput.h>
#include <wlr/render/allocator.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_alpha_modifier_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_drm.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_group.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_output_swapchain_manager.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <xkbcommon/xkbcommon.h>

#include "xdg-shell-protocol.h"
#include "util.h"
#include "drwl.h"

/* macros */
#define CLEANMASK(mask)         (mask & ~WLR_MODIFIER_CAPS)
#define VISIBLEON(C, M)         ((M) && (C)->mon == (M) && ((C)->tags & (M)->tagset[(M)->seltags]) && !(C)->isminimized)
/* the client's surface inside its border and title bar, client-relative */
#define CONTENTX(C)             ((int)(C)->bw)
#define CONTENTY(C)             ((int)(C)->bw + (C)->th)
#define CONTENTW(C)             ((C)->geom.width - 2 * (int)(C)->bw)
#define CONTENTH(C)             ((C)->geom.height - 2 * (int)(C)->bw - (C)->th)
#define TAGMASK                 ((1u << LENGTH(tags)) - 1)
#define LISTEN(E, L, H)         wl_signal_add((E), ((L)->notify = (H), (L)))
#define LISTEN_STATIC(E, H)     do { struct wl_listener *_l = ecalloc(1, sizeof(*_l)); _l->notify = (H); wl_signal_add((E), _l); } while (0)
#define TEXTW(mon, text)        (drwl_font_getwidth(mon->drw, text) + mon->lrpad)

/* enums */
enum { SchemeNorm, SchemeSel, SchemeUrg }; /* color schemes */
enum { CurNormal, CurPressed, CurMove, CurResize, CurTileResize, CurSwap }; /* cursor */
enum { XDGShell, LayerShell, X11 }; /* client types */
enum { LyrBg, LyrBottom, LyrTile, LyrFloat, LyrTop, LyrFS, LyrUnmanaged, LyrOverlay,
	LyrIMPopup, LyrBlock, NUM_LAYERS }; /* scene layers */
enum { AxisUp, AxisRight, AxisDown, AxisLeft };
enum { ClkTagBar, ClkStatus, ClkClient, ClkRoot }; /* clicks */

typedef union {
	int i;
	uint32_t ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	unsigned int mod;
	unsigned int dir;
	void (*func)(const Arg *);
	const Arg arg;
} Axis;

typedef struct {
	unsigned int click;
	unsigned int mod;
	unsigned int button;
	void (*func)(const Arg *);
	const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct {
	/* Must keep this field first */
	unsigned int type; /* XDGShell or X11* */

	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_rect *border;
	struct wlr_scene_shadow *shadow;
	struct wlr_scene_blur *blur;
	struct wlr_scene_tree *scene_surface;
	struct wl_list link;
	struct wl_list flink;
	struct wlr_box geom; /* layout-relative, includes border */
	struct wlr_box prev; /* layout-relative, includes border */
	struct wlr_box bounds; /* only width and height are used */
	union {
		struct wlr_xdg_surface *xdg;
		struct wlr_xwayland_surface *xwayland;
	} surface;
	struct wlr_xdg_toplevel_decoration_v1 *decoration;
	struct wlr_ext_foreign_toplevel_handle_v1 *foreign_toplevel_handle;
	struct wlr_scene *image_capture_scene;
	struct wlr_ext_image_capture_source_v1 *image_capture_source;
	union {
		struct wlr_scene_tree *image_capture_tree;
		struct wlr_scene_surface *image_capture_scene_surface;
	} capture;
	struct wl_listener commit;
	struct wl_listener map;
	struct wl_listener maximize;
	struct wl_listener unmap;
	struct wl_listener destroy;
	struct wl_listener set_title;
	struct wl_listener fullscreen;
	struct wl_listener set_decoration_mode;
	struct wl_listener destroy_decoration;
	unsigned int bw;
	/* title bar (deco.h): th is its height, 0 without one (fullscreen, or
	 * the client draws its own decorations) */
	int th;
	struct wlr_scene_rect *tbar, *tsep;
	struct wlr_scene_buffer *title, *buttons;
	int titlew; /* width the title was last drawn for */
	uint32_t tags;
	int isfloating, isurgent, isfullscreen, ismaximized, isminimized;
	struct wlr_box premax; /* floating geometry before maximizing */
	/* fair layout: grid cell and column/row weights (0 = unset) */
	int col, row;
	float colper, rowper, oldcolper, oldrowper;
	struct wlr_box dragbox;
	/* animation: the scene node slides from (ax, ay) to geom and, when
	 * fading, fades in; animstart is its start in ms, 0 when idle */
	int ax, ay, placed, fading;
	float alpha; /* of the fade-in, 1 when shown */
	int64_t animstart;
} Client;

typedef struct {
	uint32_t mod;
	xkb_keysym_t keysym;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	struct wlr_keyboard_group *wlr_group;
	struct wlr_keyboard *virtual_keyboard;

	xkb_keysym_t keysyms[2];
	uint32_t mods;
	struct wl_event_source *key_repeat_source;

	struct wl_listener modifiers;
	struct wl_listener key;
	struct wl_listener destroy;
} KeyboardGroup;

typedef struct {
	/* Must keep this field first */
	unsigned int type; /* LayerShell */

	Monitor *mon;
	struct wlr_scene_tree *scene;
	struct wlr_scene_tree *popups;
	struct wlr_scene_layer_surface_v1 *scene_layer;
	struct wlr_scene_blur *blur; /* NULL on the background layer */
	struct wl_list link;
	int mapped;
	struct wlr_layer_surface_v1 *layer_surface;

	struct wl_listener destroy;
	struct wl_listener unmap;
	struct wl_listener surface_commit;
} LayerSurface;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

typedef struct {
	const char *id;         /* app_id */
	const char *const *cmd; /* launches it */
} DockApp;

typedef struct {
	struct wlr_buffer base;
	struct wl_listener release;
	bool busy;
	Img *image;
	uint32_t data[];
} Buffer;

struct Monitor {
	struct wl_list link;
	struct wlr_output *wlr_output;
	struct wlr_scene_output *scene_output;
	struct wlr_scene_buffer *scene_buffer; /* bar buffer */
	struct wlr_scene_blur *barblur; /* glass behind the bar */
	struct wlr_scene_rect *fullscreen_bg; /* See createmon() for info */
	struct wlr_scene_optimized_blur *blur;
	struct wl_listener frame;
	struct wl_listener destroy;
	struct wl_listener request_state;
	struct wl_listener destroy_lock_surface;
	struct wlr_session_lock_surface_v1 *lock_surface;
	struct wlr_box m; /* monitor area, layout-relative */
	struct {
		int width, height;
		int real_width, real_height; /* non-scaled */
		float scale;
	} b; /* bar area */
	struct wlr_box w; /* window area, layout-relative */
	struct wl_list layers[4]; /* LayerSurface.link */
	const Layout *lt[2];
	unsigned int seltags;
	unsigned int sellt;
	uint32_t tagset[2];
	float mfact;
	int gamma_lut_changed;
	int nmaster;
	char ltsymbol[16];
	int asleep;
	Drwl *drw;
	Fnt *boldfnt, *logofnt; /* menu bar: app name, logo (drw->font: the rest) */
	Buffer *pool[2];
	int lrpad;
	int logox[2], tagx[9][2]; /* menu bar parts, in bar buffer px */
	struct Dock *dock;
	struct wlr_scene_buffer *wallpaper;
};

typedef struct {
	const char *name;
	float mfact;
	int nmaster;
	float scale;
	const Layout *lt;
	enum wl_output_transform rr;
	int x, y;
} MonitorRule;

typedef struct {
	struct wlr_pointer_constraint_v1 *constraint;
	struct wl_listener destroy;
} PointerConstraint;

typedef struct {
	const char *id;
	const char *title;
	uint32_t tags;
	int isfloating;
	int monitor;
} Rule;

typedef struct {
	struct wlr_scene_tree *scene;

	struct wlr_session_lock_v1 *lock;
	struct wl_listener new_surface;
	struct wl_listener unlock;
	struct wl_listener destroy;
} SessionLock;

/* function declarations */
static void applybounds(Client *c, struct wlr_box *bbox);
static int animate(Client *c, int64_t now);
static void applyrules(Client *c);
static void arrange(Monitor *m);
static void clientalpha(Client *c, float alpha);
static int64_t nowms(void);
static void setopacity(struct wlr_scene_buffer *buffer, int sx, int sy, void *data);
static void arrangelayer(Monitor *m, struct wl_list *list,
		struct wlr_box *usable_area, int exclusive);
static void arrangelayers(Monitor *m);
static void axisnotify(struct wl_listener *listener, void *data);
static bool baracceptsinput(struct wlr_scene_buffer *buffer, double *sx, double *sy);
static void bufdestroy(struct wlr_buffer *buffer);
static bool bufdatabegin(struct wlr_buffer *buffer, uint32_t flags,
		void **data, uint32_t *format, size_t *stride);
static void bufdataend(struct wlr_buffer *buffer);
static Buffer *bufmon(Monitor *m);
static void bufrelease(struct wl_listener *listener, void *data);
static void buttonpress(struct wl_listener *listener, void *data);
static void capturerequest(struct wl_listener *listener, void *data);
static void chvt(const Arg *arg);
static void checkidleinhibitor(struct wlr_surface *exclude);
static void cleanup(void);
static void cleanupmon(struct wl_listener *listener, void *data);
static void cleanuplisteners(void);
static void closemon(Monitor *m);
static void commitlayersurfacenotify(struct wl_listener *listener, void *data);
static void commitnotify(struct wl_listener *listener, void *data);
static void commitpopup(struct wl_listener *listener, void *data);
static int cornerradius(Client *c);
static void createdecoration(struct wl_listener *listener, void *data);
static void createidleinhibitor(struct wl_listener *listener, void *data);
static void createkeyboard(struct wlr_keyboard *keyboard);
static KeyboardGroup *createkeyboardgroup(void);
static void createlayersurface(struct wl_listener *listener, void *data);
static void createlocksurface(struct wl_listener *listener, void *data);
static void createmon(struct wl_listener *listener, void *data);
static void createnotify(struct wl_listener *listener, void *data);
static void createpointer(struct wlr_pointer *pointer);
static void createpointerconstraint(struct wl_listener *listener, void *data);
static void createpopup(struct wl_listener *listener, void *data);
static void cursorconstrain(struct wlr_pointer_constraint_v1 *constraint);
static void cursorframe(struct wl_listener *listener, void *data);
static void cursorwarptohint(void);
static void destroydecoration(struct wl_listener *listener, void *data);
static void destroydragicon(struct wl_listener *listener, void *data);
static void destroyidleinhibitor(struct wl_listener *listener, void *data);
static void destroylayersurfacenotify(struct wl_listener *listener, void *data);
static void destroylock(SessionLock *lock, int unlocked);
static void destroylocksurface(struct wl_listener *listener, void *data);
static void destroynotify(struct wl_listener *listener, void *data);
static Client *dirclient(enum wlr_direction dir, int tiledonly);
static void destroypointerconstraint(struct wl_listener *listener, void *data);
static void destroysessionlock(struct wl_listener *listener, void *data);
static void destroykeyboardgroup(struct wl_listener *listener, void *data);
static Monitor *dirtomon(enum wlr_direction dir);
static void drawbar(Monitor *m);
static void drawbars(void);
static void drawtooltip(void);
static void drawwallpaper(Monitor *m);
static int barpress(uint32_t button);
static void fair(Monitor *m);
static void fairbegin(Client *gc);
static void fairresize(Client *gc, int offx, int offy);
static void exchangeclient(const Arg *arg);
static void focusclient(Client *c, int lift);
static void focusdir(const Arg *arg);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static Client *focustop(Monitor *m);
static void fullscreennotify(struct wl_listener *listener, void *data);
static void gpureset(struct wl_listener *listener, void *data);
static void handlesig(int signo);
static void incnmaster(const Arg *arg);
static void inputdevice(struct wl_listener *listener, void *data);
static void touchdown(struct wl_listener *listener, void *data);
static void touchframe(struct wl_listener *listener, void *data);
static void touchmotion(struct wl_listener *listener, void *data);
static void touchup(struct wl_listener *listener, void *data);
static const Key *keybinding(uint32_t mods, xkb_keysym_t sym);
static void keypress(struct wl_listener *listener, void *data);
static void keypressmod(struct wl_listener *listener, void *data);
static int keyrepeat(void *data);
static void killclient(const Arg *arg);
static void locksession(struct wl_listener *listener, void *data);
static void mapnotify(struct wl_listener *listener, void *data);
static void maximizenotify(struct wl_listener *listener, void *data);
static void monocle(Monitor *m);
static Monitor *mirrorof(Monitor *m);
static void motionabsolute(struct wl_listener *listener, void *data);
static void motionnotify(uint32_t time, struct wlr_input_device *device, double sx,
		double sy, double sx_unaccel, double sy_unaccel);
static void motionrelative(struct wl_listener *listener, void *data);
static void moveresize(const Arg *arg);
static void minimizesel(const Arg *arg);
static void grabclient(Client *c, unsigned int mode);
static void shadowupdate(Client *c);
static void dockchanged(void);
static void outputmgrapply(struct wl_listener *listener, void *data);
static void outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test);
static void outputmgrtest(struct wl_listener *listener, void *data);
static void pointerfocus(Client *c, LayerSurface* l, struct wlr_surface *surface,
		double sx, double sy, uint32_t time);
static void powermgrsetmode(struct wl_listener *listener, void *data);
static void quit(const Arg *arg);
static void rendermon(struct wl_listener *listener, void *data);
static void requestdecorationmode(struct wl_listener *listener, void *data);
static void requeststartdrag(struct wl_listener *listener, void *data);
static void requestmonstate(struct wl_listener *listener, void *data);
static void resize(Client *c, struct wlr_box geo, int interact);
static void resizedir(const Arg *arg);
static void roundbuffer(struct wlr_scene_buffer *buffer, int sx, int sy, void *data);
static void roundlayer(struct wlr_scene_buffer *buffer, int sx, int sy, void *data);
static struct wlr_scene_blur *glassbelow(struct wlr_scene_node *node);
static void run(char *startup_cmd);
static void setcursor(struct wl_listener *listener, void *data);
static void setcursorshape(struct wl_listener *listener, void *data);
static void setfloating(Client *c, int floating);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setmon(Client *c, Monitor *m, uint32_t newtags);
static void setpsel(struct wl_listener *listener, void *data);
static void setsel(struct wl_listener *listener, void *data);
static void setup(void);
static void spawn(const Arg *arg);
static void startdrag(struct wl_listener *listener, void *data);
static int statusin(int fd, unsigned int mask, void *data);
static void parsestatus(const char *line);
static void swapclients(Client *a, Client *b);
static void swapdrop(Client *c);
static void tag(const Arg *arg);
static void tagfollow(const Arg *arg);
static void tagmon(const Arg *arg);
static void tile(Monitor *m);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void togglemaximize(const Arg *arg);
static void togglefullscreen(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unlocksession(struct wl_listener *listener, void *data);
static void unmaplayersurfacenotify(struct wl_listener *listener, void *data);
static void unmapnotify(struct wl_listener *listener, void *data);
static void updatemons(struct wl_listener *listener, void *data);
static void updatebar(Monitor *m);
static void updatetooltip(void);
static void updatetitle(struct wl_listener *listener, void *data);
static void urgent(struct wl_listener *listener, void *data);
static void view(const Arg *arg);
static void virtualkeyboard(struct wl_listener *listener, void *data);
static void virtualpointer(struct wl_listener *listener, void *data);
static Monitor *xytomon(double x, double y);
static void xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny);
static void zoom(const Arg *arg);

/* variables */
static pid_t child_pid = -1;
static int locked;
static void *exclusive_focus;
static Client *focused_client;
static struct wl_display *dpy;
static struct wl_event_loop *event_loop;
static struct wlr_backend *backend;
static struct wlr_scene *scene;
static struct wlr_scene_tree *layers[NUM_LAYERS];
static struct wlr_scene_tree *drag_icon;
/* Map from ZWLR_LAYER_SHELL_* constants to Lyr* enum */
static const int layermap[] = { LyrBg, LyrBottom, LyrTop, LyrOverlay };
static struct wlr_renderer *drw;
static struct wlr_allocator *alloc;
static struct wlr_compositor *compositor;
static struct wlr_session *session;

static struct wlr_xdg_shell *xdg_shell;
static struct wlr_xdg_activation_v1 *activation;
static struct wlr_xdg_decoration_manager_v1 *xdg_decoration_mgr;
static struct wl_list clients; /* tiling order */
static struct wl_list fstack;  /* focus order */
static struct wlr_idle_notifier_v1 *idle_notifier;
static struct wlr_idle_inhibit_manager_v1 *idle_inhibit_mgr;
static struct wlr_layer_shell_v1 *layer_shell;
static struct wlr_output_manager_v1 *output_mgr;
static struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_mgr;
static struct wlr_virtual_pointer_manager_v1 *virtual_pointer_mgr;
static struct wlr_cursor_shape_manager_v1 *cursor_shape_mgr;
static struct wlr_output_power_manager_v1 *power_mgr;
static struct wlr_ext_foreign_toplevel_list_v1 *foreign_toplevel_list;
static struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1 *ext_foreign_toplevel_image_capture_source_manager_v1;

static struct wlr_pointer_constraints_v1 *pointer_constraints;
static struct wlr_relative_pointer_manager_v1 *relative_pointer_mgr;
static struct wlr_pointer_constraint_v1 *active_constraint;

static struct wlr_cursor *cursor;
static struct wlr_xcursor_manager *cursor_mgr;

static struct wlr_scene_rect *root_bg;
static struct wlr_session_lock_manager_v1 *session_lock_mgr;
static struct wlr_scene_rect *locked_bg;
static struct wlr_session_lock_v1 *cur_lock;

static struct wlr_seat *seat;
static KeyboardGroup *kb_group;
static unsigned int cursor_mode;
static Client *grabc;
static int grabcx, grabcy; /* client-relative */
static uint32_t lastresize; /* time of the last fair resize while dragging */

static struct wlr_output_layout *output_layout;
static struct wlr_box sgeom;
static struct wl_list mons;
static Monitor *selmon;
static int consumed[KEY_MAX + 1];

static char stext[512];
/* Status segments and their tooltips (bar-tooltips). The status line is
 * "seg0\x1fseg1...\x1etip0\x1ftip1...": \x1f separates segments, \x1e
 * starts the tooltips, which follow in the same order. */
#define MAXSEG 12
static int nseg;
static size_t segoff[MAXSEG + 1];  /* start of each segment in stext */
static int segx[MAXSEG + 1];       /* its x in the bar buffer, see drawbar() */
static char tips[MAXSEG][128];
static Monitor *segmon;            /* monitor whose bar shows the status */
static int tipseg = -1;            /* segment whose tooltip is shown */
static struct wlr_scene_buffer *tooltip;
static struct wlr_scene_blur *tooltipblur;
static struct wl_event_source *status_event_source;

static const struct wlr_buffer_impl buffer_impl = {
    .destroy = bufdestroy,
    .begin_data_ptr_access = bufdatabegin,
    .end_data_ptr_access = bufdataend,
};

/* global event handlers */
static struct wl_listener cursor_axis = {.notify = axisnotify};
static struct wl_listener cursor_button = {.notify = buttonpress};
static struct wl_listener cursor_frame = {.notify = cursorframe};
static struct wl_listener touch_down = {.notify = touchdown};
static struct wl_listener touch_frame = {.notify = touchframe};
static struct wl_listener touch_motion = {.notify = touchmotion};
static struct wl_listener touch_up = {.notify = touchup};
static int touch_devices;
static int32_t touch_emulated = -1; /* touch point acting as the left button */
/* Layout position of each touch point's surface, by touch_id. shortcut: assumes
 * libinput's small slot ids (< 16), true for touchscreens; widen if ever not. */
static double touch_ox[16], touch_oy[16];
static struct wl_listener cursor_motion = {.notify = motionrelative};
static struct wl_listener cursor_motion_absolute = {.notify = motionabsolute};
static struct wl_listener gpu_reset = {.notify = gpureset};
static struct wl_listener layout_change = {.notify = updatemons};
static struct wl_listener new_idle_inhibitor = {.notify = createidleinhibitor};
static struct wl_listener new_input_device = {.notify = inputdevice};
static struct wl_listener new_virtual_keyboard = {.notify = virtualkeyboard};
static struct wl_listener new_virtual_pointer = {.notify = virtualpointer};
static struct wl_listener new_pointer_constraint = {.notify = createpointerconstraint};
static struct wl_listener new_output = {.notify = createmon};
static struct wl_listener new_xdg_toplevel = {.notify = createnotify};
static struct wl_listener new_xdg_popup = {.notify = createpopup};
static struct wl_listener new_xdg_decoration = {.notify = createdecoration};
static struct wl_listener new_layer_surface = {.notify = createlayersurface};
static struct wl_listener output_mgr_apply = {.notify = outputmgrapply};
static struct wl_listener output_mgr_test = {.notify = outputmgrtest};
static struct wl_listener output_power_mgr_set_mode = {.notify = powermgrsetmode};
static struct wl_listener request_activate = {.notify = urgent};
static struct wl_listener request_cursor = {.notify = setcursor};
static struct wl_listener request_set_psel = {.notify = setpsel};
static struct wl_listener request_set_sel = {.notify = setsel};
static struct wl_listener request_set_cursor_shape = {.notify = setcursorshape};
static struct wl_listener request_start_drag = {.notify = requeststartdrag};
static struct wl_listener start_drag = {.notify = startdrag};
static struct wl_listener new_session_lock = {.notify = locksession};
static struct wl_listener new_foreign_toplevel_capture_request = {.notify = capturerequest};


/* configuration, allows nested code to access above variables */
#include "config.h"

/* attempt to encapsulate suck into one file */
#include "client.h"

#include "ime.h"

#include "deco.h"
#include "icons.h"
#include "dock.h"

/* function implementations */
void
applybounds(Client *c, struct wlr_box *bbox)
{
	/* set minimum client size to 1 */
	c->geom.width = MAX(1 + 2 * (int)c->bw + 3 * c->th, c->geom.width);
	c->geom.height = MAX(1 + 2 * (int)c->bw + c->th, c->geom.height);

	if (!c->isfullscreen) {
		struct wlr_box min = {0}, max = {0};
		client_get_size_hints(c, &max, &min);
		c->geom.width = MAX(min.width + 2 * (int)c->bw, c->geom.width);
		c->geom.height = MAX(min.height + 2 * (int)c->bw + c->th, c->geom.height);
		/* Some clients set their max size to INT_MAX, which does not violate the
		 * protocol but it's unnecesary, as they can set their max size to zero. */
		if (max.width > 0 && 2 * (int)c->bw <= INT_MAX - max.width) /* Checks for overflow */
			c->geom.width = MIN(max.width + 2 * (int)c->bw, c->geom.width);
		if (max.height > 0 && 2 * (int)c->bw <= INT_MAX - max.height) /* Checks for overflow */
			c->geom.height = MIN(max.height + 2 * (int)c->bw + c->th, c->geom.height);
	}

	if (c->geom.x >= bbox->x + bbox->width)
		c->geom.x = bbox->x + bbox->width - c->geom.width;
	if (c->geom.y >= bbox->y + bbox->height)
		c->geom.y = bbox->y + bbox->height - c->geom.height;
	if (c->geom.x + c->geom.width <= bbox->x)
		c->geom.x = bbox->x;
	if (c->geom.y + c->geom.height <= bbox->y)
		c->geom.y = bbox->y;
}

void
applyrules(Client *c)
{
	/* rule matching */
	const char *appid, *title;
	uint32_t newtags = 0;
	int i;
	const Rule *r;
	Monitor *mon = selmon, *m;

	appid = client_get_appid(c);
	title = client_get_title(c);

	for (r = rules; r < END(rules); r++) {
		if ((!r->title || strstr(title, r->title))
				&& (!r->id || strstr(appid, r->id))) {
			c->isfloating = r->isfloating;
			newtags |= r->tags;
			i = 0;
			wl_list_for_each(m, &mons, link) {
				if (r->monitor == i++)
					mon = m;
			}
		}
	}

	c->isfloating |= client_is_float_type(c);
	setmon(c, mon, newtags);
}

void
arrange(Monitor *m)
{
	Client *c;

	if (!m->wlr_output->enabled)
		return;

	wl_list_for_each(c, &clients, link) {
		if (c->mon == m) {
			wlr_scene_node_set_enabled(&c->scene->node, VISIBLEON(c, m));
			client_set_suspended(c, !VISIBLEON(c, m)
					&& (!c->image_capture_source
					|| wl_list_empty(&c->image_capture_source->resources)));
		}
	}

	wlr_scene_node_set_enabled(&m->fullscreen_bg->node,
			(c = focustop(m)) && c->isfullscreen);

	strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof(m->ltsymbol));

	/* We move all clients (except fullscreen and unmanaged) to LyrTile while
	 * in floating layout to avoid "real" floating clients be always on top */
	wl_list_for_each(c, &clients, link) {
		if (c->mon != m || c->scene->node.parent == layers[LyrFS])
			continue;

		wlr_scene_node_reparent(&c->scene->node,
				(!m->lt[m->sellt]->arrange && c->isfloating)
						? layers[LyrTile]
						: (m->lt[m->sellt]->arrange && c->isfloating)
								? layers[LyrFloat]
								: c->scene->node.parent);
	}

	if (m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, m) && c->ismaximized && !c->isfullscreen)
			resize(c, m->w, 0);
		else if (VISIBLEON(c, m) && !c->placed)
			resize(c, c->geom, 0); /* fade in new windows the layout didn't place */
	motionnotify(0, NULL, 0, 0, 0, 0);
	checkidleinhibitor(NULL);
}

void
arrangelayer(Monitor *m, struct wl_list *list, struct wlr_box *usable_area, int exclusive)
{
	LayerSurface *l;
	struct wlr_box full_area = m->m;

	wl_list_for_each(l, list, link) {
		struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;

		if (!layer_surface->initialized)
			continue;

		if (exclusive != (layer_surface->current.exclusive_zone > 0))
			continue;

		wlr_scene_layer_surface_v1_configure(l->scene_layer, &full_area, usable_area);
		wlr_scene_node_set_position(&l->popups->node, l->scene->node.x, l->scene->node.y);
	}
}

void
arrangelayers(Monitor *m)
{
	int i;
	struct wlr_box usable_area = m->m;
	LayerSurface *l;
	uint32_t layers_above_shell[] = {
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		ZWLR_LAYER_SHELL_V1_LAYER_TOP,
	};
	if (!m->wlr_output->enabled)
		return;

	if (m->scene_buffer->node.enabled) {
		usable_area.height -= m->b.real_height;
		usable_area.y += topbar ? m->b.real_height : 0;
	}
	if (!mirrorof(m))
		usable_area.height -= dockreserve();

	/* Arrange exclusive surfaces from top->bottom */
	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 1);

	if (!wlr_box_equal(&usable_area, &m->w)) {
		m->w = usable_area;
		arrange(m);
	}

	/* Arrange non-exclusive surfaces from top->bottom */
	for (i = 3; i >= 0; i--)
		arrangelayer(m, &m->layers[i], &usable_area, 0);

	/* Find topmost keyboard interactive layer that has indicated it wants
	 * exclusive access to the keyboard, if such a layer exists */
	for (i = 0; i < (int)LENGTH(layers_above_shell); i++) {
		wl_list_for_each_reverse(l, &m->layers[layers_above_shell[i]], link) {
			if (locked ||
					l->layer_surface->current.keyboard_interactive != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE ||
					!l->mapped)
				continue;
			/* Deactivate the focused client. */
			focusclient(NULL, 0);
			exclusive_focus = l;
			client_notify_enter(l->layer_surface->surface, wlr_seat_get_keyboard(seat));
			return;
		}
	}
}

void
axisnotify(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an axis event,
	 * for example when you move the scroll wheel. */
	struct wlr_pointer_axis_event *event = data;
	const Axis *a;
	unsigned int adir;
	struct wlr_keyboard *keyboard;
	uint32_t mods;

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL)
		adir = event->delta > 0 ? AxisDown : AxisUp;
	else
		adir = event->delta > 0 ? AxisRight : AxisLeft;
	keyboard = wlr_seat_get_keyboard(seat);
	mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
	for (a = axes; a < END(axes); a++) {
		if (CLEANMASK(mods) == CLEANMASK(a->mod) && adir == a->dir && a->func) {
			a->func(&a->arg);
			return;
		}
	}

	/* Notify the client with pointer focus of the axis event. */
	wlr_seat_pointer_notify_axis(seat,
			event->time_msec, event->orientation, event->delta,
			event->delta_discrete, event->source, event->relative_direction);
}

bool
baracceptsinput(struct wlr_scene_buffer *buffer, double *sx, double *sy)
{
	return true;
}

void
bufdestroy(struct wlr_buffer *wlr_buffer)
{
	Buffer *buf = wl_container_of(wlr_buffer, buf, base);
	if (buf->busy)
		wl_list_remove(&buf->release.link);
	drwl_image_destroy(buf->image);
	free(buf);
}

bool
bufdatabegin(struct wlr_buffer *wlr_buffer, uint32_t flags,
		void **data, uint32_t *format, size_t *stride)
{
	Buffer *buf = wl_container_of(wlr_buffer, buf, base);

	if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE) return false;

	*data   = buf->data;
	*stride = wlr_buffer->width * 4;
	*format = DRM_FORMAT_ARGB8888;

	return true;
}

void
bufdataend(struct wlr_buffer *wlr_buffer)
{
}

Buffer *
bufmon(Monitor *m)
{
	size_t i;
	Buffer *buf = NULL;

	for (i = 0; i < LENGTH(m->pool); i++) {
		if (m->pool[i]) {
			if (m->pool[i]->busy)
				continue;
			buf = m->pool[i];
			break;
		}

		buf = ecalloc(1, sizeof(Buffer) + (m->b.width * 4 * m->b.height));
		buf->image = drwl_image_create(NULL, m->b.width, m->b.height, buf->data);
		wlr_buffer_init(&buf->base, &buffer_impl, m->b.width, m->b.height);
		m->pool[i] = buf;
		break;
	}
	if (!buf)
		return NULL;

	buf->busy = true;
	LISTEN(&buf->base.events.release, &buf->release, bufrelease);
	wlr_buffer_lock(&buf->base);
	drwl_setimage(m->drw, buf->image);
	return buf;
}

void
bufrelease(struct wl_listener *listener, void *data)
{
	Buffer *buf = wl_container_of(listener, buf, release);
	buf->busy = false;
	wl_list_remove(&buf->release.link);
}

void
buttonpress(struct wl_listener *listener, void *data)
{
	unsigned int click;
	struct wlr_pointer_button_event *event = data;
	struct wlr_keyboard *keyboard;
	uint32_t mods;
	unsigned int mode;
	Client *c = NULL;
	LayerSurface *l = NULL;
	const Button *b;

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	click = ClkRoot;
	xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
	if (c)
		click = ClkClient;

	switch (event->state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		cursor_mode = CurPressed;
		selmon = xytomon(cursor->x, cursor->y);
		if (locked)
			break;

		if (!c && !exclusive_focus && (barpress(event->button) || dockpress(event->button)))
			return;
		keyboard = wlr_seat_get_keyboard(seat);
		mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
		if (!c && !exclusive_focus && event->button == BTN_LEFT && !CLEANMASK(mods)
				&& decopress(1, event->time_msec))
			return;

		/* Change focus if the button was _pressed_ over a client
		   or a layer surface with on-demand keyboard interactivity */
		xytonode(cursor->x, cursor->y, NULL, &c, &l, NULL, NULL);
		if (click == ClkClient && c && (!client_is_unmanaged(c) || client_wants_focus(c))) {
			focusclient(c, 1);
		} else if (l && l->layer_surface->current.keyboard_interactive) {
			focusclient(NULL, 0);
			client_notify_enter(l->layer_surface->surface, wlr_seat_get_keyboard(seat));
		}

		keyboard = wlr_seat_get_keyboard(seat);
		mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
		for (b = buttons; b < END(buttons); b++) {
			if (CLEANMASK(mods) == CLEANMASK(b->mod) && event->button == b->button && click == b->click && b->func) {
				b->func(&b->arg);
				return;
			}
		}
		break;
	case WL_POINTER_BUTTON_STATE_RELEASED:
		if (event->button == BTN_LEFT && decopress(0, event->time_msec)) {
			cursor_mode = CurNormal;
			return;
		}
		/* If you released any buttons, we exit interactive move/resize mode. */
		if (!locked && cursor_mode != CurNormal && cursor_mode != CurPressed) {
			mode = cursor_mode;
			cursor_mode = CurNormal;
			/* Update the cursor */
			wlr_seat_pointer_clear_focus(seat);
			motionnotify(0, NULL, 0, 0, 0, 0);
			/* Drop the window off on its new monitor; resized tiles stay */
			selmon = xytomon(cursor->x, cursor->y);
			if (mode == CurSwap)
				swapdrop(grabc);
			else if (mode != CurTileResize)
				setmon(grabc, selmon, 0);
			grabc = NULL;
			return;
		}
		cursor_mode = CurNormal;
		break;
	}
	/* If the event wasn't handled by the compositor, notify the client with
	 * pointer focus that a button press has occurred */
	wlr_seat_pointer_notify_button(seat,
			event->time_msec, event->button, event->state);
}

void
capturerequest(struct wl_listener *listener, void *data)
{
	struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request *request = data;
	Client *view = request->toplevel_handle->data;

	if (!view->image_capture_source) {
		view->image_capture_source = wlr_ext_image_capture_source_v1_create_with_scene_node(
				&view->image_capture_scene->tree.node, event_loop, alloc, drw);
		if (!view->image_capture_source)
			return;
	}

	wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(
		request, view->image_capture_source);

	client_set_suspended(view, 0);
}

void
chvt(const Arg *arg)
{
	wlr_session_change_vt(session, arg->ui);
}

void
checkidleinhibitor(struct wlr_surface *exclude)
{
	int inhibited = 0, unused_lx, unused_ly;
	struct wlr_idle_inhibitor_v1 *inhibitor;
	wl_list_for_each(inhibitor, &idle_inhibit_mgr->inhibitors, link) {
		struct wlr_surface *surface = wlr_surface_get_root_surface(inhibitor->surface);
		struct wlr_scene_tree *tree = surface->data;
		if (exclude != surface && (bypass_surface_visibility || (!tree
				|| wlr_scene_node_coords(&tree->node, &unused_lx, &unused_ly)))) {
			inhibited = 1;
			break;
		}
	}

	wlr_idle_notifier_v1_set_inhibited(idle_notifier, inhibited);
}

void
cleanup(void)
{
	cleanuplisteners();
	wl_display_destroy_clients(dpy);
	if (child_pid > 0) {
		kill(-child_pid, SIGTERM);
		waitpid(child_pid, NULL, 0);
	}
	wlr_xcursor_manager_destroy(cursor_mgr);

	destroykeyboardgroup(&kb_group->destroy, NULL);

	input_method_relay_finish(input_method_relay);
	input_method_relay = NULL;

	/* If it's not destroyed manually, it will cause a use-after-free of wlr_seat.
	 * Destroy it until it's fixed on the wlroots side */
	wlr_backend_destroy(backend);

	wl_display_destroy(dpy);
	/* Destroy after the wayland display (when the monitors are already destroyed)
	   to avoid destroying them with an invalid scene output. */
	wlr_scene_node_destroy(&scene->tree.node);

	drwl_fini();
}

void
cleanupmon(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, destroy);
	LayerSurface *l, *tmp;
	size_t i;

	/* m->layers[i] are intentionally not unlinked */
	for (i = 0; i < LENGTH(m->layers); i++) {
		wl_list_for_each_safe(l, tmp, &m->layers[i], link)
			wlr_layer_surface_v1_destroy(l->layer_surface);
	}

	for (i = 0; i < LENGTH(m->pool); i++)
		wlr_buffer_drop(&m->pool[i]->base);

	if (segmon == m) {
		segmon = NULL;
		tipseg = -1;
		drawtooltip();
	}

	drwl_setimage(m->drw, NULL);
	drwl_destroy(m->drw);

	wl_list_remove(&m->destroy.link);
	wl_list_remove(&m->frame.link);
	wl_list_remove(&m->link);
	wl_list_remove(&m->request_state.link);
	if (m->lock_surface)
		destroylocksurface(&m->destroy_lock_surface, NULL);
	m->wlr_output->data = NULL;
	wlr_output_layout_remove(output_layout, m->wlr_output);
	wlr_scene_output_destroy(m->scene_output);

	closemon(m);
	wlr_scene_node_destroy(&m->fullscreen_bg->node);
	wlr_scene_node_destroy(&m->blur->node);
	wlr_scene_node_destroy(&m->scene_buffer->node);
	wlr_scene_node_destroy(&m->barblur->node);
	wlr_scene_node_destroy(&m->wallpaper->node);
	dockdestroy(m);
	if (m->boldfnt)
		drwl_font_destroy(m->boldfnt);
	if (m->logofnt)
		drwl_font_destroy(m->logofnt);
	free(m);
}

void
cleanuplisteners(void)
{
	wl_list_remove(&cursor_axis.link);
	wl_list_remove(&cursor_button.link);
	wl_list_remove(&cursor_frame.link);
	wl_list_remove(&cursor_motion.link);
	wl_list_remove(&cursor_motion_absolute.link);
	wl_list_remove(&gpu_reset.link);
	wl_list_remove(&new_idle_inhibitor.link);
	wl_list_remove(&layout_change.link);
	wl_list_remove(&new_input_device.link);
	wl_list_remove(&new_virtual_keyboard.link);
	wl_list_remove(&new_virtual_pointer.link);
	wl_list_remove(&new_pointer_constraint.link);
	wl_list_remove(&new_output.link);
	wl_list_remove(&new_xdg_toplevel.link);
	wl_list_remove(&new_xdg_decoration.link);
	wl_list_remove(&new_xdg_popup.link);
	wl_list_remove(&new_layer_surface.link);
	wl_list_remove(&output_mgr_apply.link);
	wl_list_remove(&output_mgr_test.link);
	wl_list_remove(&output_power_mgr_set_mode.link);
	wl_list_remove(&request_activate.link);
	wl_list_remove(&request_cursor.link);
	wl_list_remove(&request_set_psel.link);
	wl_list_remove(&request_set_sel.link);
	wl_list_remove(&request_set_cursor_shape.link);
	wl_list_remove(&request_start_drag.link);
	wl_list_remove(&start_drag.link);
	wl_list_remove(&new_session_lock.link);
	wl_list_remove(&new_foreign_toplevel_capture_request.link);
}

void
closemon(Monitor *m)
{
	/* update selmon if needed and
	 * move closed monitor's clients to the focused one */
	Client *c;
	int i = 0, nmons = wl_list_length(&mons);
	if (!nmons) {
		selmon = NULL;
	} else if (m == selmon) {
		do /* don't switch to disabled mons */
			selmon = wl_container_of(mons.next, selmon, link);
		while (!selmon->wlr_output->enabled && i++ < nmons);

		if (!selmon->wlr_output->enabled)
			selmon = NULL;
	}

	wl_list_for_each(c, &clients, link) {
		if (c->isfloating && c->geom.x > m->m.width)
			resize(c, (struct wlr_box){.x = c->geom.x - m->w.width, .y = c->geom.y,
					.width = c->geom.width, .height = c->geom.height}, 0);
		if (c->mon == m)
			setmon(c, selmon, c->tags);
	}
	focusclient(focustop(selmon), 1);
	drawbars();
}

void
commitlayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, surface_commit);
	struct wlr_layer_surface_v1 *layer_surface = l->layer_surface;
	struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->current.layer]];
	struct wlr_layer_surface_v1_state old_state;

	if (l->layer_surface->initial_commit) {
		client_set_scale(layer_surface->surface, l->mon->wlr_output->scale);

		/* Temporarily set the layer's current state to pending
		 * so that we can easily arrange it */
		old_state = l->layer_surface->current;
		l->layer_surface->current = l->layer_surface->pending;
		arrangelayers(l->mon);
		l->layer_surface->current = old_state;
		return;
	}

	if (l->blur) {
		wlr_scene_blur_set_size(l->blur, layer_surface->surface->current.width,
				layer_surface->surface->current.height);
		wlr_scene_node_for_each_buffer(&l->scene->node, roundlayer, NULL);
	}

	if (layer_surface == exclusive_focus
			&& layer_surface->current.keyboard_interactive !=
				ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE)
		exclusive_focus = NULL;

	/* The cached blur of the background layer must be redrawn when it changes */
	if (scene_layer == layers[LyrBg] || l->scene->node.parent == layers[LyrBg])
		wlr_scene_optimized_blur_mark_dirty(l->mon->blur);

	if (layer_surface->current.committed == 0 && l->mapped == layer_surface->surface->mapped)
		return;
	l->mapped = layer_surface->surface->mapped;

	if (scene_layer != l->scene->node.parent) {
		wlr_scene_node_reparent(&l->scene->node, scene_layer);
		wl_list_remove(&l->link);
		wl_list_insert(&l->mon->layers[layer_surface->current.layer], &l->link);
		wlr_scene_node_reparent(&l->popups->node, (layer_surface->current.layer
				< ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer));
	}

	arrangelayers(l->mon);
}

void
commitnotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, commit);

	if (c->surface.xdg->initial_commit) {
		/*
		 * Get the monitor this client will be rendered on
		 * Note that if the user set a rule in which the client is placed on
		 * a different monitor based on its title, this will likely select
		 * a wrong monitor.
		 */
		applyrules(c);
		if (c->mon) {
			client_set_scale(client_surface(c), c->mon->wlr_output->scale);
		}
		setmon(c, NULL, 0); /* Make sure to reapply rules in mapnotify() */

		wlr_xdg_toplevel_set_wm_capabilities(c->surface.xdg->toplevel,
				WLR_XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN);
		if (c->decoration)
			requestdecorationmode(&c->set_decoration_mode, c->decoration);
		wlr_xdg_toplevel_set_size(c->surface.xdg->toplevel, 0, 0);
		if (!c->isfloating)
			client_set_tiled(c, WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT |
					WLR_EDGE_RIGHT);
		return;
	}

	resize(c, c->geom, (c->isfloating && !c->isfullscreen));
	/* Subsurfaces may have been added or resized */
	if (client_surface(c)->mapped)
		wlr_scene_node_for_each_buffer(&c->scene_surface->node, roundbuffer, c);
}

int
cornerradius(Client *c)
{
	if (c->isfullscreen || (corner_radius_only_floating && !c->isfloating
			&& c->mon && c->mon->lt[c->mon->sellt]->arrange))
		return 0;
	return corner_radius;
}

void
commitpopup(struct wl_listener *listener, void *data)
{
	struct wlr_surface *surface = data;
	struct wlr_xdg_popup *popup = wlr_xdg_popup_try_from_wlr_surface(surface);
	LayerSurface *l = NULL;
	Client *c = NULL;
	struct wlr_box box;
	int type = -1;

	if (!popup->base->initial_commit)
		return;

	type = toplevel_from_wlr_surface(popup->base->surface, &c, &l);
	if (!popup->parent || type < 0)
		return;
	popup->base->surface->data = wlr_scene_xdg_surface_create(
			popup->parent->data, popup->base);
	if ((l && !l->mon) || (c && !c->mon)) {
		wlr_xdg_popup_destroy(popup);
		return;
	}
	box = type == LayerShell ? l->mon->m : c->mon->w;
	box.x -= (type == LayerShell ? l->scene->node.x : c->geom.x + CONTENTX(c));
	box.y -= (type == LayerShell ? l->scene->node.y : c->geom.y + CONTENTY(c));
	wlr_xdg_popup_unconstrain_from_box(popup, &box);
	wl_list_remove(&listener->link);
	free(listener);
}

void
createdecoration(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_toplevel_decoration_v1 *deco = data;
	Client *c = deco->toplevel->base->data;
	c->decoration = deco;

	LISTEN(&deco->events.request_mode, &c->set_decoration_mode, requestdecorationmode);
	LISTEN(&deco->events.destroy, &c->destroy_decoration, destroydecoration);

	requestdecorationmode(&c->set_decoration_mode, deco);
	decoset(c);
}

void
createidleinhibitor(struct wl_listener *listener, void *data)
{
	struct wlr_idle_inhibitor_v1 *idle_inhibitor = data;
	LISTEN_STATIC(&idle_inhibitor->events.destroy, destroyidleinhibitor);

	checkidleinhibitor(NULL);
}

void
createkeyboard(struct wlr_keyboard *keyboard)
{
	/* Set the keymap to match the group keymap */
	wlr_keyboard_set_keymap(keyboard, kb_group->wlr_group->keyboard.keymap);

	/* Add the new keyboard to the group */
	wlr_keyboard_group_add_keyboard(kb_group->wlr_group, keyboard);
}

KeyboardGroup *
createkeyboardgroup(void)
{
	KeyboardGroup *group = ecalloc(1, sizeof(*group));
	struct xkb_context *context;
	struct xkb_keymap *keymap;

	group->wlr_group = wlr_keyboard_group_create();
	group->wlr_group->data = group;

	/* Prepare an XKB keymap and assign it to the keyboard group. */
	context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!(keymap = xkb_keymap_new_from_names(context, &xkb_rules,
				XKB_KEYMAP_COMPILE_NO_FLAGS)))
		die("failed to compile keymap");

	wlr_keyboard_set_keymap(&group->wlr_group->keyboard, keymap);
	xkb_keymap_unref(keymap);
	xkb_context_unref(context);

	wlr_keyboard_set_repeat_info(&group->wlr_group->keyboard, repeat_rate, repeat_delay);

	/* Set up listeners for keyboard events */
	LISTEN(&group->wlr_group->keyboard.events.key, &group->key, keypress);
	LISTEN(&group->wlr_group->keyboard.events.modifiers, &group->modifiers, keypressmod);

	group->key_repeat_source = wl_event_loop_add_timer(event_loop, keyrepeat, group);

	/* A seat can only have one keyboard, but this is a limitation of the
	 * Wayland protocol - not wlroots. We assign all connected keyboards to the
	 * same wlr_keyboard_group, which provides a single wlr_keyboard interface for
	 * all of them. Set this combined wlr_keyboard as the seat keyboard.
	 */
	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	return group;
}

void
createlayersurface(struct wl_listener *listener, void *data)
{
	struct wlr_layer_surface_v1 *layer_surface = data;
	LayerSurface *l;
	struct wlr_surface *surface = layer_surface->surface;
	struct wlr_scene_tree *scene_layer = layers[layermap[layer_surface->pending.layer]];

	if (!layer_surface->output
			&& !(layer_surface->output = selmon ? selmon->wlr_output : NULL)) {
		wlr_layer_surface_v1_destroy(layer_surface);
		return;
	}

	l = layer_surface->data = ecalloc(1, sizeof(*l));
	l->type = LayerShell;
	LISTEN(&surface->events.commit, &l->surface_commit, commitlayersurfacenotify);
	LISTEN(&surface->events.unmap, &l->unmap, unmaplayersurfacenotify);
	LISTEN(&layer_surface->events.destroy, &l->destroy, destroylayersurfacenotify);

	l->layer_surface = layer_surface;
	l->mon = layer_surface->output->data;
	l->scene_layer = wlr_scene_layer_surface_v1_create(scene_layer, layer_surface);
	l->scene = l->scene_layer->tree;
	l->popups = surface->data = wlr_scene_tree_create(layer_surface->current.layer
			< ZWLR_LAYER_SHELL_V1_LAYER_TOP ? layers[LyrTop] : scene_layer);
	l->scene->node.data = l->popups->node.data = l;
	if (layer_surface->pending.layer != ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND) {
		l->blur = wlr_scene_blur_create(l->scene, 0, 0);
		wlr_scene_node_lower_to_bottom(&l->blur->node);
		wlr_scene_blur_set_corner_radius(l->blur, corner_radius);
		wlr_scene_blur_set_glass(l->blur, 1);
		wlr_scene_blur_set_should_only_blur_bottom_layer(l->blur, 1);
	}

	wl_list_insert(&l->mon->layers[layer_surface->pending.layer],&l->link);
	wlr_surface_send_enter(surface, layer_surface->output);
}

void
createlocksurface(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, new_surface);
	struct wlr_session_lock_surface_v1 *lock_surface = data;
	Monitor *m = lock_surface->output->data;
	struct wlr_scene_tree *scene_tree = lock_surface->surface->data
			= wlr_scene_subsurface_tree_create(lock->scene, lock_surface->surface);
	m->lock_surface = lock_surface;

	wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
	wlr_session_lock_surface_v1_configure(lock_surface, m->m.width, m->m.height);

	LISTEN(&lock_surface->events.destroy, &m->destroy_lock_surface, destroylocksurface);

	if (m == selmon)
		client_notify_enter(lock_surface->surface, wlr_seat_get_keyboard(seat));
}

void
createmon(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new output (aka a display or
	 * monitor) becomes available. */
	struct wlr_output *wlr_output = data;
	const MonitorRule *r;
	size_t i;
	struct wlr_output_state state;
	Monitor *m;

	if (!wlr_output_init_render(wlr_output, alloc, drw))
		return;

	m = wlr_output->data = ecalloc(1, sizeof(*m));
	m->wlr_output = wlr_output;

	for (i = 0; i < LENGTH(m->layers); i++)
		wl_list_init(&m->layers[i]);

	wlr_output_state_init(&state);
	/* Initialize monitor state using configured rules */
	m->tagset[0] = m->tagset[1] = 1;
	for (r = monrules; r < END(monrules); r++) {
		if (!r->name || strstr(wlr_output->name, r->name)) {
			m->m.x = r->x;
			m->m.y = r->y;
			m->mfact = r->mfact;
			m->nmaster = r->nmaster;
			m->lt[0] = r->lt;
			m->lt[1] = &layouts[LENGTH(layouts) > 1 && r->lt != &layouts[1]];
			strncpy(m->ltsymbol, m->lt[m->sellt]->symbol, sizeof(m->ltsymbol));
			wlr_output_state_set_scale(&state, r->scale);
			wlr_output_state_set_transform(&state, r->rr);
			break;
		}
	}

	/* The mode is a tuple of (width, height, refresh rate), and each
	 * monitor supports only a specific set of modes. We just pick the
	 * monitor's preferred mode; a more sophisticated compositor would let
	 * the user configure it. */
	wlr_output_state_set_mode(&state, wlr_output_preferred_mode(wlr_output));

	/* Set up event listeners */
	LISTEN(&wlr_output->events.frame, &m->frame, rendermon);
	LISTEN(&wlr_output->events.destroy, &m->destroy, cleanupmon);
	LISTEN(&wlr_output->events.request_state, &m->request_state, requestmonstate);

	wlr_output_state_set_enabled(&state, 1);
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);

	if (!(m->drw = drwl_create()))
		die("failed to create drwl context");

	m->wallpaper = wlr_scene_buffer_create(layers[LyrBg], NULL);
	wlr_scene_node_lower_to_bottom(&m->wallpaper->node);
	m->scene_buffer = wlr_scene_buffer_create(layers[LyrBottom], NULL);
	m->scene_buffer->point_accepts_input = baracceptsinput;
	m->barblur = glassbelow(&m->scene_buffer->node);
	updatebar(m);
	dockcreate(m);

	wl_list_insert(&mons, &m->link);
	drawbars();

	/* The xdg-protocol specifies:
	 *
	 * If the fullscreened surface is not opaque, the compositor must make
	 * sure that other screen content not part of the same surface tree (made
	 * up of subsurfaces, popups or similarly coupled surfaces) are not
	 * visible below the fullscreened surface.
	 *
	 */
	/* updatemons() will resize and set correct position */
	m->fullscreen_bg = wlr_scene_rect_create(layers[LyrFS], 0, 0, fullscreen_bg);
	wlr_scene_node_set_enabled(&m->fullscreen_bg->node, 0);

	/* Caches the blur of everything below it, which tiled windows show */
	m->blur = wlr_scene_optimized_blur_create(&scene->tree, 0, 0);
	wlr_scene_node_place_above(&m->blur->node, &layers[LyrBg]->node);

	/* Adds this to the output layout in the order it was configured.
	 *
	 * The output layout utility automatically adds a wl_output global to the
	 * display, which Wayland clients can see to find out information about the
	 * output (such as DPI, scale factor, manufacturer, etc).
	 */
	m->scene_output = wlr_scene_output_create(scene, wlr_output);
	if (m->m.x == -1 && m->m.y == -1)
		wlr_output_layout_add_auto(output_layout, wlr_output);
	else
		wlr_output_layout_add(output_layout, wlr_output, m->m.x, m->m.y);
}

void
createnotify(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client creates a new toplevel (application window). */
	struct wlr_xdg_toplevel *toplevel = data;
	Client *c = NULL;

	/* Allocate a Client for this surface */
	c = toplevel->base->data = ecalloc(1, sizeof(*c));
	c->surface.xdg = toplevel->base;
	c->bw = borderpx;
	c->alpha = 1.0f;

	LISTEN(&toplevel->base->surface->events.commit, &c->commit, commitnotify);
	LISTEN(&toplevel->base->surface->events.map, &c->map, mapnotify);
	LISTEN(&toplevel->base->surface->events.unmap, &c->unmap, unmapnotify);
	LISTEN(&toplevel->events.destroy, &c->destroy, destroynotify);
	LISTEN(&toplevel->events.request_fullscreen, &c->fullscreen, fullscreennotify);
	LISTEN(&toplevel->events.request_maximize, &c->maximize, maximizenotify);
	LISTEN(&toplevel->events.set_title, &c->set_title, updatetitle);
}

void
createpointer(struct wlr_pointer *pointer)
{
	struct libinput_device *device;
	if (wlr_input_device_is_libinput(&pointer->base)
			&& (device = wlr_libinput_get_device_handle(&pointer->base))) {

		if (libinput_device_config_tap_get_finger_count(device)) {
			libinput_device_config_tap_set_enabled(device, tap_to_click);
			libinput_device_config_tap_set_drag_enabled(device, tap_and_drag);
			libinput_device_config_tap_set_drag_lock_enabled(device, drag_lock);
			libinput_device_config_tap_set_button_map(device, button_map);
		}

		if (libinput_device_config_scroll_has_natural_scroll(device))
			libinput_device_config_scroll_set_natural_scroll_enabled(device, natural_scrolling);

		if (libinput_device_config_dwt_is_available(device))
			libinput_device_config_dwt_set_enabled(device, disable_while_typing);

		if (libinput_device_config_left_handed_is_available(device))
			libinput_device_config_left_handed_set(device, left_handed);

		if (libinput_device_config_middle_emulation_is_available(device))
			libinput_device_config_middle_emulation_set_enabled(device, middle_button_emulation);

		if (libinput_device_config_scroll_get_methods(device) != LIBINPUT_CONFIG_SCROLL_NO_SCROLL)
			libinput_device_config_scroll_set_method(device, scroll_method);

		if (libinput_device_config_click_get_methods(device) != LIBINPUT_CONFIG_CLICK_METHOD_NONE)
			libinput_device_config_click_set_method(device, click_method);

		if (libinput_device_config_send_events_get_modes(device))
			libinput_device_config_send_events_set_mode(device, send_events_mode);

		if (libinput_device_config_accel_is_available(device)) {
			libinput_device_config_accel_set_profile(device, accel_profile);
			libinput_device_config_accel_set_speed(device, accel_speed);
		}
	}

	wlr_cursor_attach_input_device(cursor, &pointer->base);
}

void
createpointerconstraint(struct wl_listener *listener, void *data)
{
	PointerConstraint *pointer_constraint = ecalloc(1, sizeof(*pointer_constraint));
	pointer_constraint->constraint = data;
	LISTEN(&pointer_constraint->constraint->events.destroy,
			&pointer_constraint->destroy, destroypointerconstraint);
}

void
createpopup(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client (either xdg-shell or layer-shell)
	 * creates a new popup. */
	struct wlr_xdg_popup *popup = data;
	LISTEN_STATIC(&popup->base->surface->events.commit, commitpopup);
}

void
cursorconstrain(struct wlr_pointer_constraint_v1 *constraint)
{
	if (active_constraint == constraint)
		return;

	if (active_constraint)
		wlr_pointer_constraint_v1_send_deactivated(active_constraint);

	active_constraint = constraint;
	wlr_pointer_constraint_v1_send_activated(constraint);
}

void
cursorframe(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits a frame
	 * event. Frame events are sent after regular pointer events to group
	 * multiple events together. For instance, two axis events may happen at the
	 * same time, in which case a frame event won't be sent in between. */
	/* Notify the client with pointer focus of the frame event. */
	wlr_seat_pointer_notify_frame(seat);
}

void
cursorwarptohint(void)
{
	Client *c = NULL;
	double sx = active_constraint->current.cursor_hint.x;
	double sy = active_constraint->current.cursor_hint.y;

	toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
	if (c && active_constraint->current.cursor_hint.enabled) {
		wlr_cursor_warp(cursor, NULL, sx + c->geom.x + CONTENTX(c), sy + c->geom.y + CONTENTY(c));
		wlr_seat_pointer_warp(active_constraint->seat, sx, sy);
	}
}

void
destroydecoration(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, destroy_decoration);
	c->decoration = NULL;

	wl_list_remove(&c->destroy_decoration.link);
	wl_list_remove(&c->set_decoration_mode.link);
	decoset(c);
}

void
destroydragicon(struct wl_listener *listener, void *data)
{
	/* Focus enter isn't sent during drag, so refocus the focused node. */
	focusclient(focustop(selmon), 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
	wl_list_remove(&listener->link);
	free(listener);
}

void
destroyidleinhibitor(struct wl_listener *listener, void *data)
{
	/* `data` is the wlr_surface of the idle inhibitor being destroyed,
	 * at this point the idle inhibitor is still in the list of the manager */
	checkidleinhibitor(wlr_surface_get_root_surface(data));
	wl_list_remove(&listener->link);
	free(listener);
}

void
destroylayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, destroy);

	wl_list_remove(&l->link);
	wl_list_remove(&l->destroy.link);
	wl_list_remove(&l->unmap.link);
	wl_list_remove(&l->surface_commit.link);
	wlr_scene_node_destroy(&l->scene->node);
	wlr_scene_node_destroy(&l->popups->node);
	free(l);
}

void
destroylock(SessionLock *lock, int unlock)
{
	wlr_seat_keyboard_notify_clear_focus(seat);
	if ((locked = !unlock))
		goto destroy;

	wlr_scene_node_set_enabled(&locked_bg->node, 0);

	focusclient(focustop(selmon), 0);
	motionnotify(0, NULL, 0, 0, 0, 0);

destroy:
	wl_list_remove(&lock->new_surface.link);
	wl_list_remove(&lock->unlock.link);
	wl_list_remove(&lock->destroy.link);

	wlr_scene_node_destroy(&lock->scene->node);
	cur_lock = NULL;
	free(lock);
}

void
destroylocksurface(struct wl_listener *listener, void *data)
{
	Monitor *m = wl_container_of(listener, m, destroy_lock_surface);
	struct wlr_session_lock_surface_v1 *surface, *lock_surface = m->lock_surface;

	m->lock_surface = NULL;
	wl_list_remove(&m->destroy_lock_surface.link);

	if (lock_surface->surface != seat->keyboard_state.focused_surface)
		return;

	if (locked && cur_lock && !wl_list_empty(&cur_lock->surfaces)) {
		surface = wl_container_of(cur_lock->surfaces.next, surface, link);
		client_notify_enter(surface->surface, wlr_seat_get_keyboard(seat));
	} else if (!locked) {
		focusclient(focustop(selmon), 1);
	} else {
		wlr_seat_keyboard_clear_focus(seat);
	}
}

void
destroynotify(struct wl_listener *listener, void *data)
{
	/* Called when the xdg_toplevel is destroyed. */
	Client *c = wl_container_of(listener, c, destroy);
	wl_list_remove(&c->destroy.link);
	wl_list_remove(&c->set_title.link);
	wl_list_remove(&c->fullscreen.link);
	{
		wl_list_remove(&c->commit.link);
		wl_list_remove(&c->map.link);
		wl_list_remove(&c->unmap.link);
		wl_list_remove(&c->maximize.link);
		if (c->decoration) {
			wl_list_remove(&c->set_decoration_mode.link);
			wl_list_remove(&c->destroy_decoration.link);
		}
	}
	free(c);
}

void
destroypointerconstraint(struct wl_listener *listener, void *data)
{
	PointerConstraint *pointer_constraint = wl_container_of(listener, pointer_constraint, destroy);

	if (active_constraint == pointer_constraint->constraint) {
		cursorwarptohint();
		active_constraint = NULL;
	}

	wl_list_remove(&pointer_constraint->destroy.link);
	free(pointer_constraint);
}

void
destroysessionlock(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, destroy);
	destroylock(lock, 0);
}

void
destroykeyboardgroup(struct wl_listener *listener, void *data)
{
	KeyboardGroup *group = wl_container_of(listener, group, destroy);
	wl_event_source_remove(group->key_repeat_source);
	wl_list_remove(&group->key.link);
	wl_list_remove(&group->modifiers.link);
	wl_list_remove(&group->destroy.link);
	wlr_keyboard_group_destroy(group->wlr_group);
	free(group);
}

/* The nearest visible client whose centre lies in direction dir from the
 * focused client's centre (the algorithm river uses) */
Client *
dirclient(enum wlr_direction dir, int tiledonly)
{
	Client *c, *sel = focustop(selmon), *best = NULL;
	int sx, sy, dx, dy;
	long d, bestd = LONG_MAX;

	if (!sel || sel->isfullscreen || (tiledonly && sel->isfloating))
		return NULL;
	sx = sel->geom.x + sel->geom.width / 2;
	sy = sel->geom.y + sel->geom.height / 2;

	wl_list_for_each(c, &clients, link) {
		if (c == sel || !VISIBLEON(c, selmon) || (tiledonly && c->isfloating))
			continue;
		dx = c->geom.x + c->geom.width / 2 - sx;
		dy = c->geom.y + c->geom.height / 2 - sy;
		if (!dx && !dy)
			continue;
		if (abs(dy) > abs(dx) ? dir != (dy > 0 ? WLR_DIRECTION_DOWN : WLR_DIRECTION_UP)
				: dir != (dx > 0 ? WLR_DIRECTION_RIGHT : WLR_DIRECTION_LEFT))
			continue;
		d = (long)dx * dx + (long)dy * dy;
		if (d < bestd) {
			bestd = d;
			best = c;
		}
	}
	return best;
}

Monitor *
dirtomon(enum wlr_direction dir)
{
	struct wlr_output *next;
	if (!wlr_output_layout_get(output_layout, selmon->wlr_output))
		return selmon;
	if ((next = wlr_output_layout_adjacent_output(output_layout,
			dir, selmon->wlr_output, selmon->m.x, selmon->m.y)))
		return next->data;
	if ((next = wlr_output_layout_farthest_output(output_layout,
			dir ^ (WLR_DIRECTION_LEFT | WLR_DIRECTION_RIGHT |
					WLR_DIRECTION_UP | WLR_DIRECTION_DOWN),
			selmon->wlr_output, selmon->m.x, selmon->m.y)))
		return next->data;
	return selmon;
}

/* Text at x in the bar buffer with a soft shadow, as macOS draws the menu
 * bar over the wallpaper; returns its width */
static int
bartext(Monitor *m, Fnt *font, int x, const char *text, uint32_t color)
{
	uint32_t shadow[3] = { menubar_shadow, 0 }, fg[3] = { color, 0 };
	int w;
	Fnt *old = m->drw->font;

	drwl_setfont(m->drw, font);
	w = (int)drwl_font_getwidth(m->drw, text);
	if (menubar_shadow & 0xff) {
		drwl_setscheme(m->drw, shadow);
		drwl_text(m->drw, x, (int)m->b.scale, (unsigned)w + 1, (unsigned)m->b.height, 0, text, 0);
	}
	drwl_setscheme(m->drw, fg);
	drwl_text(m->drw, x, 0, (unsigned)w + 1, (unsigned)m->b.height, 0, text, 0);
	drwl_setfont(m->drw, old);
	return w;
}

/* The macOS menu bar: the logo (a menu), the focused app's name in bold,
 * the occupied tags like menu titles, the status on the right */
void
drawbar(Monitor *m)
{
	int x, tw = 0, pad;
	uint32_t i, occ = 0, urg = 0;
	float sc = m->wlr_output->scale;
	Client *c, *sel;
	Buffer *buf;
	pixman_color_t clear = { 0 };
	char num[4];

	wlr_scene_node_set_enabled(&m->barblur->node, m->scene_buffer->node.enabled && menubar_glass);
	if (!m->scene_buffer->node.enabled)
		return;
	if (!(buf = bufmon(m)))
		return;
	pixman_image_fill_rectangles(PIXMAN_OP_SRC, buf->image, &clear, 1,
			&(pixman_rectangle16_t){ 0, 0, (uint16_t)m->b.width, (uint16_t)m->b.height });
	if (menubar_glass) {
		uint32_t bg[3] = { 0, colors[SchemeNorm][ColBg] };
		drwl_setscheme(m->drw, bg);
		drwl_rect(m->drw, 0, 0, (unsigned)m->b.width, (unsigned)m->b.height, 1, 1);
	}
	pad = (int)(menubar_padding * sc);

	/* status on the right, only on the selected monitor */
	if (m == selmon) {
		tw = (int)drwl_font_getwidth(m->drw, stext);
		bartext(m, m->drw->font, m->b.width - tw - pad, stext, menubar_text);
		/* remember where each status segment starts, for tooltips */
		for (i = 0; i <= (uint32_t)nseg; i++) {
			char ch = stext[segoff[i]];
			stext[segoff[i]] = '\0';
			segx[i] = m->b.width - tw - pad + (int)drwl_font_getwidth(m->drw, stext);
			stext[segoff[i]] = ch;
		}
		segmon = m;
	}

	x = pad;
	m->logox[0] = 0;
	x += bartext(m, m->logofnt, x, menubar_logo, menubar_text);
	m->logox[1] = x + pad / 2;
	x += (int)(menubar_spacing * sc);

	sel = focustop(m);
	if (sel) {
		App *a = appget(client_get_appid(sel), 0);
		x += bartext(m, m->boldfnt, x, a->name, menubar_text) + (int)(menubar_spacing * sc);
	}

	wl_list_for_each(c, &clients, link) {
		if (c->mon != m)
			continue;
		occ |= c->tags;
		if (c->isurgent)
			urg |= c->tags;
	}
	for (i = 0; i < LENGTH(tags); i++) {
		int sel_ = m->tagset[m->seltags] & 1 << i;
		m->tagx[i][0] = m->tagx[i][1] = -1;
		if (!sel_ && !(occ & 1 << i))
			continue;
		snprintf(num, sizeof(num), "%s", tags[i]);
		m->tagx[i][0] = x - (int)(menubar_spacing * sc) / 2;
		x += bartext(m, m->drw->font, x, num, urg & 1 << i ? menubar_urgent
				: sel_ ? menubar_text : menubar_dim);
		m->tagx[i][1] = x + (int)(menubar_spacing * sc) / 2;
		x += (int)(menubar_spacing * sc);
	}

	wlr_scene_buffer_set_dest_size(m->scene_buffer,
		m->b.real_width, m->b.real_height);
	wlr_scene_node_set_position(&m->scene_buffer->node, m->m.x,
		m->m.y + (topbar ? 0 : m->m.height - m->b.real_height));
	wlr_scene_blur_set_size(m->barblur, m->b.real_width, m->b.real_height);
	wlr_scene_node_set_position(&m->barblur->node, m->scene_buffer->node.x,
		m->scene_buffer->node.y);
	wlr_scene_buffer_set_buffer(m->scene_buffer, &buf->base);
	wlr_buffer_unlock(&buf->base);
}

/* A click on the menu bar: the logo opens its menu, a tag is viewed */
int
barpress(uint32_t button)
{
	Monitor *m = xytomon(cursor->x, cursor->y);
	struct wlr_scene_node *node;
	Arg a;
	int i, x;

	if (!m || !(node = wlr_scene_node_at(&layers[LyrBottom]->node, cursor->x, cursor->y, NULL, NULL))
			|| node != &m->scene_buffer->node)
		return 0;
	if (button != BTN_LEFT)
		return 1;
	x = (int)((cursor->x - m->m.x) * m->wlr_output->scale);
	if (x >= m->logox[0] && x < m->logox[1]) {
		a.v = logomenucmd;
		spawn(&a);
		return 1;
	}
	for (i = 0; i < (int)LENGTH(tags); i++)
		if (x >= m->tagx[i][0] && x < m->tagx[i][1]) {
			a.ui = 1u << i;
			selmon = m;
			view(&a);
			return 1;
		}
	return 1;
}

/* The wallpaper: config.h's image scaled to cover the monitor, or a
 * gradient in the colours of macOS' wallpapers if there is none */
void
drawwallpaper(Monitor *m)
{
	static uint32_t *img;
	static int iw, ih, tried;
	int w, h;
	Buffer *buf;

	if (!m->wlr_output->enabled)
		return;
	wlr_output_transformed_resolution(m->wlr_output, &w, &h);
	if (w <= 0 || h <= 0)
		return;
	if (!tried++ && wallpaper)
		img = img_load(wallpaper, 0, &iw, &ih);
	buf = newbuf(w, h);
	if (img) {
		pixman_image_t *src = pixman_image_create_bits(PIXMAN_a8r8g8b8, iw, ih, img, iw * 4);
		double f = MIN((double)iw / w, (double)ih / h); /* cover */
		pixman_transform_t t;
		pixman_transform_init_scale(&t, pixman_double_to_fixed(f), pixman_double_to_fixed(f));
		pixman_transform_translate(NULL, &t, pixman_double_to_fixed((iw - w * f) / 2),
				pixman_double_to_fixed((ih - h * f) / 2));
		pixman_image_set_transform(src, &t);
		pixman_image_set_filter(src, f > 1.5 ? PIXMAN_FILTER_GOOD : PIXMAN_FILTER_BILINEAR, NULL, 0);
		pixman_image_composite32(PIXMAN_OP_SRC, src, NULL, buf->image, 0, 0, 0, 0, 0, 0, w, h);
		pixman_image_unref(src);
	} else {
		/* a deep blue base with soft blue, violet and amber glows */
		static const struct { float x, y, r; uint32_t c; } glow[] = {
			{ 0.15f, 0.95f, 0.85f, 0xff1f5bff },
			{ 0.80f, 0.20f, 0.70f, 0xffa23be0 },
			{ 0.95f, 1.05f, 0.55f, 0xffff8a3d },
			{ 0.45f, 0.10f, 0.45f, 0xff2bb5ff },
		};
		pixman_gradient_stop_t base[2] = {
			{ 0, { 0x0700, 0x0d00, 0x2a00, 0xffff } },
			{ pixman_int_to_fixed(1), { 0x1600, 0x0a00, 0x2600, 0xffff } },
		};
		pixman_point_fixed_t p0 = { 0, 0 }, p1 = { 0, pixman_int_to_fixed(h) };
		pixman_image_t *g = pixman_image_create_linear_gradient(&p0, &p1, base, 2);
		size_t i;
		pixman_image_composite32(PIXMAN_OP_SRC, g, NULL, buf->image, 0, 0, 0, 0, 0, 0, w, h);
		pixman_image_unref(g);
		for (i = 0; i < LENGTH(glow); i++) {
			uint16_t r = (uint16_t)((glow[i].c >> 16 & 0xff) * 0x101);
			uint16_t gr = (uint16_t)((glow[i].c >> 8 & 0xff) * 0x101);
			uint16_t b = (uint16_t)((glow[i].c & 0xff) * 0x101);
			/* premultiplied stops fading out to transparent */
			pixman_gradient_stop_t stops[3] = {
				{ 0, { (uint16_t)(r * 0.85), (uint16_t)(gr * 0.85), (uint16_t)(b * 0.85), 0xd999 } },
				{ pixman_double_to_fixed(0.45), { (uint16_t)(r * 0.35), (uint16_t)(gr * 0.35), (uint16_t)(b * 0.35), 0x5999 } },
				{ pixman_int_to_fixed(1), { 0, 0, 0, 0 } },
			};
			float rad = glow[i].r * (float)MAX(w, h);
			pixman_point_fixed_t c = { pixman_double_to_fixed(glow[i].x * w),
				pixman_double_to_fixed(glow[i].y * h) };
			g = pixman_image_create_radial_gradient(&c, &c, 0,
					pixman_double_to_fixed(rad), stops, 3);
			pixman_image_composite32(PIXMAN_OP_OVER, g, NULL, buf->image, 0, 0, 0, 0, 0, 0, w, h);
			pixman_image_unref(g);
		}
	}
	wlr_scene_buffer_set_buffer(m->wallpaper, &buf->base);
	wlr_buffer_drop(&buf->base);
	wlr_scene_buffer_set_dest_size(m->wallpaper, m->m.width, m->m.height);
	wlr_scene_node_set_position(&m->wallpaper->node, m->m.x, m->m.y);
	wlr_scene_optimized_blur_mark_dirty(m->blur);
}

void
drawbars(void)
{
	Monitor *m = NULL;

	wl_list_for_each(m, &mons, link)
		drawbar(m);
}

/* Port of mangowm's "fair" layout (src/layout/horizontal.c, fair_core) and
 * its grid resize (src/layout/arrange.c, resize_tile_grid_fair), gappx apart.
 * cols = ceil(sqrt(n)); clients fill the grid column by column. The first
 * (cols - n % cols) columns have n / cols rows, the others one row more.
 * Every column and row has a weight that resizing changes. */
/* Swap the focused tiled client with the tiled client in direction arg->i */
void
exchangeclient(const Arg *arg)
{
	Client *c, *sel = focustop(selmon);
	if (!(c = dirclient(arg->i, 1)))
		return;
	swapclients(sel, c);
	arrange(selmon);
}

void
fair(Monitor *m)
{
	int i, n = 0, cols, baserows, rem, firstcols, firstcount, maxrows, ci, ri;
	float *colper, *rowper, *colx, *colw, *rowy, *rowh, sum, avail, next;
	struct wlr_box a;
	Client *c, **v;

	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, m) && !c->isfloating)
			n++;
	if (n == 0)
		return;

	for (cols = 0; cols <= n; cols++)
		if (cols * cols >= n)
			break;
	baserows = n / cols;
	rem = n % cols;
	firstcols = cols - rem;
	firstcount = firstcols * baserows;
	maxrows = baserows + (rem > 0);

	v = ecalloc(n, sizeof(*v));
	colper = ecalloc(3 * cols + 5 * maxrows, sizeof(float));
	colx = colper + cols;
	colw = colx + cols;
	rowper = colw + cols;
	/* rowy/rowh: [0, maxrows) for columns with baserows rows,
	 * [maxrows, 2 * maxrows) for columns with maxrows rows */
	rowy = rowper + maxrows;
	rowh = rowy + 2 * maxrows;

	i = 0;
	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, m) && !c->isfloating && i < n)
			v[i++] = c;

#define CELL(i, ci, ri) do { \
		if ((i) < firstcount) { ci = (i) / baserows; ri = (i) % baserows; } \
		else { ci = firstcols + ((i) - firstcount) / maxrows; \
			ri = ((i) - firstcount) % maxrows; } } while (0)

	/* A weight is kept by a client that stays in the same column/row,
	 * otherwise taken from any client in it, otherwise 1 */
	for (i = 0; i < n; i++) {
		CELL(i, ci, ri);
		if (v[i]->col == ci && v[i]->colper > 0.0f)
			colper[ci] = v[i]->colper;
		if (v[i]->row == ri && v[i]->rowper > 0.0f)
			rowper[ri] = v[i]->rowper;
	}
	for (i = 0; i < n; i++) {
		CELL(i, ci, ri);
		if (colper[ci] == 0.0f && v[i]->colper > 0.0f)
			colper[ci] = v[i]->colper;
		if (rowper[ri] == 0.0f && v[i]->rowper > 0.0f)
			rowper[ri] = v[i]->rowper;
	}
	for (sum = 0.0f, i = 0; i < cols; i++) {
		if (colper[i] == 0.0f)
			colper[i] = 1.0f;
		sum += colper[i];
	}
	for (i = 0; i < maxrows; i++)
		if (rowper[i] == 0.0f)
			rowper[i] = 1.0f;

	/* The last column/row takes what is left so rounding leaves no gap.
	 * Cells cover the area shrunk by half a gap; each window is then shrunk
	 * by another half, so windows are gappx apart and from the edges. */
	a = (struct wlr_box){m->w.x + gappx / 2, m->w.y + gappx / 2,
		m->w.width - gappx / 2 * 2, m->w.height - gappx / 2 * 2};
	avail = (float)a.width;
	next = (float)a.x;
	for (i = 0; i < cols; i++) {
		colx[i] = next;
		colw[i] = i == cols - 1 ? (float)(a.x + a.width) - next
				: avail * (colper[i] / sum);
		next += colw[i];
	}
	for (ri = 0; ri < 2; ri++) {
		int rows = ri ? maxrows : baserows;
		float *y = rowy + ri * maxrows, *h = rowh + ri * maxrows;
		if (ri && !rem)
			break;
		for (sum = 0.0f, i = 0; i < rows; i++)
			sum += rowper[i];
		avail = (float)a.height;
		next = (float)a.y;
		for (i = 0; i < rows; i++) {
			y[i] = next;
			h[i] = i == rows - 1 ? (float)(a.y + a.height) - next
					: avail * (rowper[i] / sum);
			next += h[i];
		}
	}

	for (i = 0; i < n; i++) {
		int off = i < firstcount ? 0 : maxrows;
		c = v[i];
		CELL(i, ci, ri);
		c->colper = colper[ci];
		c->rowper = rowper[ri];
		c->col = ci;
		c->row = ri;
		/* fullscreen and maximized clients keep their cell but not its size */
		if (c->isfullscreen || c->ismaximized)
			continue;
		resize(c, (struct wlr_box){.x = (int)colx[ci] + gappx / 2,
			.y = (int)rowy[off + ri] + gappx / 2,
			.width = (int)colw[ci] - gappx / 2 * 2,
			.height = (int)rowh[off + ri] - gappx / 2 * 2}, 0);
	}
#undef CELL

	free(v);
	free(colper);
}

/* Remember the weights at the start of a resize; offsets are applied to them */
void
fairbegin(Client *gc)
{
	Client *c;
	wl_list_for_each(c, &clients, link) {
		c->oldcolper = c->colper > 0.0f ? c->colper : 1.0f;
		c->oldrowper = c->rowper > 0.0f ? c->rowper : 1.0f;
	}
	gc->dragbox = gc->geom;
}

/* Move the boundary between gc's column/row and its neighbour by offx/offy
 * pixels (relative to fairbegin()). The neighbour is the next column/row,
 * or the previous one for the last column/row. */
void
fairresize(Client *gc, int offx, int offy)
{
	Monitor *m = gc->mon;
	Client *c;
	int n = 0, cols, baserows, adjc, adjr;
	int maxcol = -1, maxrow = -1, mincol = INT_MAX, minrow = INT_MAX;
	float dx, dy, d, p, my, adj, newmy, newadj, adjoldc = -1.0f, adjoldr = -1.0f;

	if (!m || gc->isfloating || gc->isfullscreen || gc->ismaximized)
		return;
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen || c->ismaximized)
			continue;
		n++;
		maxcol = MAX(maxcol, c->col);
		maxrow = MAX(maxrow, c->row);
		mincol = MIN(mincol, c->col);
		minrow = MIN(minrow, c->row);
	}
	if (n <= 1)
		return;

	dx = (float)offx * gc->oldcolper / (float)gc->dragbox.width;
	dy = (float)offy * gc->oldrowper / (float)gc->dragbox.height;
	adjc = gc->col + 1;
	adjr = gc->row + 1;
	if (gc->col == maxcol) {
		adjc = gc->col - 1;
		dx = -dx;
	}
	if (gc->row == maxrow) {
		adjr = gc->row - 1;
		dy = -dy;
	}
	if (gc->col == mincol && adjc != gc->col + 1) {
		adjc = gc->col + 1;
		dx = -dx;
	}
	if (gc->row == minrow && adjr != gc->row + 1) {
		adjr = gc->row + 1;
		dy = -dy;
	}

	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen || c->ismaximized)
			continue;
		if (c->col == adjc && adjoldc < 0.0f)
			adjoldc = c->oldcolper;
		if (c->row == adjr && adjoldr < 0.0f)
			adjoldr = c->oldrowper;
	}

	if (adjoldc > 0.0f) {
		my = gc->oldcolper;
		d = dx;
		if (my + d < 0.1f)
			d = 0.1f - my;
		if (adjoldc - d < 0.1f)
			d = adjoldc - 0.1f;
		newmy = my + d;
		newadj = adjoldc - d;
		wl_list_for_each(c, &clients, link) {
			if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
				continue;
			if (c->col == gc->col)
				c->colper = newmy;
			if (c->col == adjc)
				c->colper = newadj;
		}
	}

	if (adjoldr > 0.0f) {
		my = gc->oldrowper;
		adj = adjoldr;
		d = dy;
		if (my + d < 0.1f)
			d = 0.1f - my;
		if (adj - d < 0.1f)
			d = adj - 0.1f;
		newmy = my + d;
		newadj = adj - d;

		/* The extra row only exists in the last columns, so its boundary
		 * is set as a ratio between the two rows instead */
		for (cols = 0; cols <= n; cols++)
			if (cols * cols >= n)
				break;
		baserows = n / cols;
		if ((gc->row == baserows - 1 && adjr == baserows)
				|| (gc->row == baserows && adjr == baserows - 1)) {
			p = gc->row == baserows - 1 ? (my + dy) / (my + adj)
					: (adj - dy) / (my + adj);
			p = MAX(0.01f, MIN(0.99f, p));
			p = MAX(0.1f, MIN(10.0f, p / (1.0f - p)));
			newmy = gc->row == baserows - 1 ? p : 1.0f;
			newadj = gc->row == baserows - 1 ? 1.0f : p;
		}

		wl_list_for_each(c, &clients, link) {
			if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen)
				continue;
			if (c->row == gc->row)
				c->rowper = newmy;
			if (c->row == adjr)
				c->rowper = newadj;
		}
	}
	arrange(m);
}

/* Show the tooltip of status segment tipseg below (or above) the bar */
void
drawtooltip(void)
{
	Monitor *m = segmon;
	Buffer *buf;
	int w, h, x, y;
	float scale;

	if (!tooltip)
		return;
	if (tipseg < 0 || !m || !tips[tipseg][0]) {
		wlr_scene_node_set_enabled(&tooltip->node, 0);
		wlr_scene_node_set_enabled(&tooltipblur->node, 0);
		return;
	}
	scale = m->wlr_output->scale;
	w = TEXTW(m, tips[tipseg]);
	h = m->b.height;

	buf = ecalloc(1, sizeof(Buffer) + (size_t)w * 4 * h);
	buf->image = drwl_image_create(NULL, w, h, buf->data);
	wlr_buffer_init(&buf->base, &buffer_impl, w, h);
	drwl_setimage(m->drw, buf->image);
	drwl_setscheme(m->drw, colors[SchemeNorm]);
	drwl_text(m->drw, 0, 0, w, h, m->lrpad / 2, tips[tipseg], 0);
	drwl_setscheme(m->drw, colors[SchemeSel]);
	drwl_rect(m->drw, 0, 0, w, h, 0, 1); /* outline in the accent colour */
	drwl_setimage(m->drw, NULL);

	x = m->m.x + (int)(segx[tipseg] / scale);
	x = MIN(x, m->m.x + m->m.width - (int)(w / scale));
	y = topbar ? m->m.y + m->b.real_height
		: m->m.y + m->m.height - m->b.real_height - (int)(h / scale);
	wlr_scene_buffer_set_dest_size(tooltip, (int)(w / scale), (int)(h / scale));
	wlr_scene_node_set_position(&tooltip->node, x, y);
	wlr_scene_blur_set_size(tooltipblur, (int)(w / scale), (int)(h / scale));
	wlr_scene_node_set_position(&tooltipblur->node, x, y);
	wlr_scene_buffer_set_buffer(tooltip, &buf->base);
	wlr_buffer_drop(&buf->base); /* freed once the scene releases it */
	wlr_scene_node_set_enabled(&tooltip->node, 1);
	wlr_scene_node_set_enabled(&tooltipblur->node, 1);
}

void
focusclient(Client *c, int lift)
{
	struct wlr_surface *old = seat->keyboard_state.focused_surface;
	Client *prevfocus = focused_client;
	int unused_lx, unused_ly, old_client_type;
	Client *old_c = NULL;
	LayerSurface *old_l = NULL;

	if (locked)
		return;

	/* Raise client in stacking order if requested */
	if (c && lift)
		wlr_scene_node_raise_to_top(&c->scene->node);

	if (c && client_surface(c) == old)
		return;

	if ((old_client_type = toplevel_from_wlr_surface(old, &old_c, &old_l)) == XDGShell) {
		struct wlr_xdg_popup *popup, *tmp;
		wl_list_for_each_safe(popup, tmp, &old_c->surface.xdg->popups, link)
			wlr_xdg_popup_destroy(popup);
	}

	if (old_c && old_c == exclusive_focus && client_wants_focus(old_c))
		exclusive_focus = NULL;

	/* Put the new client atop the focus stack and select its monitor */
	if (c && !client_is_unmanaged(c)) {
		wl_list_remove(&c->flink);
		wl_list_insert(&fstack, &c->flink);
		selmon = c->mon;
		c->isurgent = 0;

		/* Don't change border color if there is an exclusive focus or we are
		 * handling a drag operation */
		if (!exclusive_focus && !seat->drag)
			client_set_border_color(c, (float[])PCOLOR(colors[SchemeSel][ColBorder]));
	}
	prevfocus = focused_client;

	/* If an overlay is focused, don't focus or activate the client,
	 * but only update its position in fstack to render its border with its color
	 * and focus it after the overlay is closed. */
	if (old && (!c || client_surface(c) != old)
			&& old_client_type == LayerShell && wlr_scene_node_coords(
				&old_l->scene->node, &unused_lx, &unused_ly)
			&& old_l->layer_surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP
			&& old_l->layer_surface->current.keyboard_interactive == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE)
		return;

	if (focused_client && focused_client != c && !(c && client_is_unmanaged(c))) {
		struct wlr_surface *s = client_surface(focused_client);
		if (c)
			client_set_border_color(focused_client, (float[])PCOLOR(colors[SchemeNorm][ColBorder]));
		if (s && s->mapped)
			client_activate_surface(s, 0);
		focused_client = NULL;
		if (prevfocus->scene && prevfocus->tbar) {
			decoupdate(prevfocus);
			shadowupdate(prevfocus);
		}
	}
	drawbars();

	if (!c) {
		/* With no client, all we have left is to clear focus */
		input_method_relay_set_focus(input_method_relay, NULL);
		wlr_seat_keyboard_notify_clear_focus(seat);
		return;
	}

	/* Change cursor surface */
	motionnotify(0, NULL, 0, 0, 0, 0);

	input_method_relay_set_focus(input_method_relay, client_surface(c));

	/* Have a client, so focus its top-level wlr_surface */
	client_notify_enter(client_surface(c), wlr_seat_get_keyboard(seat));

	/* Activate the new client */
	client_activate_surface(client_surface(c), 1);

	if (!client_is_unmanaged(c)) {
		focused_client = c;
		if (c->tbar) {
			decoupdate(c);
			shadowupdate(c);
		}
	}
}

/* Focus the client (tiled or floating) in direction arg->i */
void
focusdir(const Arg *arg)
{
	Client *c;
	if ((c = dirclient(arg->i, 0)))
		focusclient(c, 1);
}

void
focusmon(const Arg *arg)
{
	int i = 0, nmons = wl_list_length(&mons);
	if (nmons) {
		do /* don't switch to disabled mons */
			selmon = dirtomon(arg->i);
		while (!selmon->wlr_output->enabled && i++ < nmons);
	}
	focusclient(focustop(selmon), 1);
}

void
focusstack(const Arg *arg)
{
	/* Focus the next or previous client (in tiling order) on selmon */
	Client *c, *sel = focustop(selmon);
	if (!sel || (sel->isfullscreen && !client_has_children(sel)))
		return;
	if (arg->i > 0) {
		wl_list_for_each(c, &sel->link, link) {
			if (&c->link == &clients)
				continue; /* wrap past the sentinel node */
			if (VISIBLEON(c, selmon))
				break; /* found it */
		}
	} else {
		wl_list_for_each_reverse(c, &sel->link, link) {
			if (&c->link == &clients)
				continue; /* wrap past the sentinel node */
			if (VISIBLEON(c, selmon))
				break; /* found it */
		}
	}
	/* If only one client is visible on selmon, then c == sel */
	focusclient(c, 1);
}

/* We probably should change the name of this: it sounds like it
 * will focus the topmost client of this mon, when actually will
 * only return that client */
Client *
focustop(Monitor *m)
{
	Client *c;
	wl_list_for_each(c, &fstack, flink) {
		if (VISIBLEON(c, m))
			return c;
	}
	return NULL;
}

void
fullscreennotify(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, fullscreen);
	setfullscreen(c, client_wants_fullscreen(c));
}

void
gpureset(struct wl_listener *listener, void *data)
{
	struct wlr_renderer *old_drw = drw;
	struct wlr_allocator *old_alloc = alloc;
	struct Monitor *m;
	if (!(drw = fx_renderer_create(backend)))
		die("couldn't recreate renderer");

	if (!(alloc = wlr_allocator_autocreate(backend, drw)))
		die("couldn't recreate allocator");

	wl_list_remove(&gpu_reset.link);
	wl_signal_add(&drw->events.lost, &gpu_reset);

	wlr_compositor_set_renderer(compositor, drw);

	wl_list_for_each(m, &mons, link) {
		wlr_output_init_render(m->wlr_output, alloc, drw);
		wlr_scene_optimized_blur_mark_dirty(m->blur);
	}

	wlr_allocator_destroy(old_alloc);
	wlr_renderer_destroy(old_drw);
}

void
handlesig(int signo)
{
	if (signo == SIGCHLD)
		while (waitpid(-1, NULL, WNOHANG) > 0);
	else if (signo == SIGINT || signo == SIGTERM)
		quit(NULL);
}

void
incnmaster(const Arg *arg)
{
	if (!arg || !selmon)
		return;
	selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
	arrange(selmon);
}

void
inputdevice(struct wl_listener *listener, void *data)
{
	/* This event is raised by the backend when a new input device becomes
	 * available. */
	struct wlr_input_device *device = data;
	uint32_t caps;

	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		createkeyboard(wlr_keyboard_from_input_device(device));
		break;
	case WLR_INPUT_DEVICE_POINTER:
		createpointer(wlr_pointer_from_input_device(device));
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		wlr_cursor_attach_input_device(cursor, device);
		touch_devices++; /* shortcut: never decremented, touchscreens are built in */
		break;
	default:
		/* TODO handle other input device types */
		break;
	}

	/* We need to let the wlr_seat know what our capabilities are, which is
	 * communiciated to the client. In dwl we always have a cursor, even if
	 * there are no pointer devices, so we always include that capability. */
	/* TODO do we actually require a cursor? */
	caps = WL_SEAT_CAPABILITY_POINTER;
	if (!wl_list_empty(&kb_group->wlr_group->devices))
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	if (touch_devices)
		caps |= WL_SEAT_CAPABILITY_TOUCH;
	wlr_seat_set_capabilities(seat, caps);
}

const Key *
keybinding(uint32_t mods, xkb_keysym_t sym)
{
	/*
	 * Here we handle compositor keybindings. This is when the compositor is
	 * processing keys, rather than passing them on to the client for its own
	 * processing.
	 */
	const Key *k;
	for (k = keys; k < END(keys); k++) {
		if (CLEANMASK(mods) == CLEANMASK(k->mod) && sym == k->keysym && k->func)
			return k;
	}
	return NULL;
}

void
keypress(struct wl_listener *listener, void *data)
{
	/* This event is raised when a key is pressed or released. */
	KeyboardGroup *group = wl_container_of(listener, group, key);
	struct wlr_keyboard_key_event *event = data;

	/* Translate libinput keycode -> xkbcommon */
	uint32_t keycode = event->keycode + 8;
	struct wlr_keyboard *kb = &group->wlr_group->keyboard;
	xkb_layout_index_t layout = xkb_state_key_get_layout(kb->xkb_state, keycode);
	int handled = 0;

	// Get the keysyms for level 0 (normal) and level 1 (shifted)
	// Only one keysym for each level:
	// multiple keysyms per keycode don't really exist in the real world
	for (int i = 0; i < 2; i++)
		group->keysyms[i] = keymap_get_one_sym_by_level(kb->keymap, keycode, layout, i);
	group->mods = wlr_keyboard_get_modifiers(&group->wlr_group->keyboard);

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

	/* On _press_ if there is no active screen locker,
	 * attempt to process a compositor keybinding. */
	if (!locked && event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		for (int i = 0; i < 2; i++) {
			const Key *key = keybinding(group->mods, group->keysyms[i]);
			if (key) {
				consumed[event->keycode] = 1;
				key->func(&key->arg);
				handled = 1;
				break;
			}
		}
	}

	if (handled && group->wlr_group->keyboard.repeat_info.delay > 0)
		wl_event_source_timer_update(group->key_repeat_source,
				group->wlr_group->keyboard.repeat_info.delay);
	else
		wl_event_source_timer_update(group->key_repeat_source, 0);

	if (handled || input_method_keyboard_grab_forward_key(group, event))
		return;

	if (consumed[event->keycode]) {
		if (event->state == WL_KEYBOARD_KEY_STATE_RELEASED)
			consumed[event->keycode] = 0;
		return; /* Don't pass to the client the release event of a handled key-press */
	}
	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	/* Pass unhandled keycodes along to the client. */
	wlr_seat_keyboard_notify_key(seat, event->time_msec,
			event->keycode, event->state);
}

void
keypressmod(struct wl_listener *listener, void *data)
{
	/* This event is raised when a modifier key, such as shift or alt, is
	 * pressed. We simply communicate this to the client. */
	KeyboardGroup *group = wl_container_of(listener, group, modifiers);

	if (input_method_keyboard_grab_forward_modifiers(group))
		return;

	wlr_seat_set_keyboard(seat, &group->wlr_group->keyboard);
	/* Send modifiers to the client. */
	wlr_seat_keyboard_notify_modifiers(seat,
			&group->wlr_group->keyboard.modifiers);
}

int
keyrepeat(void *data)
{
	KeyboardGroup *group = data;
	if (group->wlr_group->keyboard.repeat_info.rate <= 0)
		return 0;

	wl_event_source_timer_update(group->key_repeat_source,
			1000 / group->wlr_group->keyboard.repeat_info.rate);

	for (int i = 0; i < 2; i++) {
		const Key *key = keybinding(group->mods, group->keysyms[i]);
		if (key) {
			key->func(&key->arg);
			break;
		}
	}

	return 0;
}

/* Minimize the focused window to the dock (unminimize: click it there) */
void
minimizesel(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel && !sel->isfullscreen)
		minimize(sel);
}

void
killclient(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		client_send_close(sel);
}

void
locksession(struct wl_listener *listener, void *data)
{
	struct wlr_session_lock_v1 *session_lock = data;
	SessionLock *lock;
	wlr_scene_node_set_enabled(&locked_bg->node, 1);
	if (cur_lock) {
		wlr_session_lock_v1_destroy(session_lock);
		return;
	}
	lock = session_lock->data = ecalloc(1, sizeof(*lock));
	focusclient(NULL, 0);

	lock->scene = wlr_scene_tree_create(layers[LyrBlock]);
	cur_lock = lock->lock = session_lock;
	locked = 1;

	LISTEN(&session_lock->events.new_surface, &lock->new_surface, createlocksurface);
	LISTEN(&session_lock->events.destroy, &lock->destroy, destroysessionlock);
	LISTEN(&session_lock->events.unlock, &lock->unlock, unlocksession);

	wlr_session_lock_v1_send_locked(session_lock);
}

void
mapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is mapped, or ready to display on-screen. */
	Client *p = NULL;
	Client *w, *c = wl_container_of(listener, c, map);
	Monitor *m;

	struct wlr_ext_foreign_toplevel_handle_v1_state foreign_toplevel_state = {
		.app_id = client_get_appid(c),
		.title = client_get_title(c),
	};

	/* Create scene tree for this client and its border */
	c->scene = client_surface(c)->data = wlr_scene_tree_create(layers[LyrTile]);
	/* Enabled later by a call to arrange() */
	wlr_scene_node_set_enabled(&c->scene->node, client_is_unmanaged(c));
	c->scene_surface = c->type == XDGShell
			? wlr_scene_xdg_surface_create(c->scene, c->surface.xdg)
			: wlr_scene_subsurface_tree_create(c->scene, client_surface(c));
	c->scene->node.data = c->scene_surface->node.data = c;

	client_get_geometry(c, &c->geom);

	/* Handle unmanaged clients first so we can return prior create borders */
	if (client_is_unmanaged(c)) {
		/* Unmanaged clients always are floating */
		wlr_scene_node_reparent(&c->scene->node, layers[LyrUnmanaged]);
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
		if (client_wants_focus(c)) {
			focusclient(c, 1);
			exclusive_focus = c;
		}
		goto unset_fullscreen;
	}

	c->border = wlr_scene_rect_create(c->scene, 0, 0,
		(float[])PCOLOR(colors[c->isurgent ? SchemeUrg : SchemeNorm][ColBorder]));
	c->border->node.data = c;
	c->shadow = wlr_scene_shadow_create(c->scene, 0, 0, 0, shadow_sigma, shadow_color);
	wlr_scene_node_lower_to_bottom(&c->shadow->node);
	c->blur = wlr_scene_blur_create(c->scene, 0, 0);
	wlr_scene_blur_set_glass(c->blur, 1);
	wlr_scene_node_lower_to_bottom(&c->blur->node);

	/* Initialize client geometry with room for border and title bar */
	decocreate(c);
	c->geom.width += 2 * c->bw;
	c->geom.height += 2 * c->bw + c->th;

	/* Insert this client into client lists: new clients go last in the
	 * tiling order, which the fair layout places at the right */
	wl_list_insert(clients.prev, &c->link);
	wl_list_insert(&fstack, &c->flink);

	c->foreign_toplevel_handle = wlr_ext_foreign_toplevel_handle_v1_create(
			foreign_toplevel_list, &foreign_toplevel_state);
	c->foreign_toplevel_handle->data = c;
	c->image_capture_scene = wlr_scene_create();
	if (c->type == XDGShell)
		c->capture.image_capture_tree = wlr_scene_xdg_surface_create(&c->image_capture_scene->tree, c->surface.xdg);


	/* Set initial monitor, tags, floating status, and focus:
	 * we always consider floating, clients that have parent and thus
	 * we set the same tags and monitor as its parent.
	 * If there is no parent, apply rules */
	if ((p = client_get_parent(c))) {
		c->isfloating = 1;
		setmon(c, p->mon, p->tags);
	} else {
		applyrules(c);
	}
	drawbars();
	dockchanged();

unset_fullscreen:
	m = c->mon ? c->mon : xytomon(c->geom.x, c->geom.y);
	wl_list_for_each(w, &clients, link) {
		if (w != c && w != p && w->isfullscreen && m == w->mon && (w->tags & c->tags))
			setfullscreen(w, 0);
	}
}

void
maximizenotify(struct wl_listener *listener, void *data)
{
	/* This event is raised when a client would like to maximize itself,
	 * typically because the user clicked on the maximize button on
	 * client-side decorations. dwl doesn't support maximization, but
	 * to conform to xdg-shell protocol we still must send a configure.
	 * Since xdg-shell protocol v5 we should ignore request of unsupported
	 * capabilities, just schedule a empty configure when the client uses <5
	 * protocol version
	 * wlr_xdg_surface_schedule_configure() is used to send an empty reply. */
	Client *c = wl_container_of(listener, c, maximize);
	if (c->surface.xdg->initialized
			&& wl_resource_get_version(c->surface.xdg->toplevel->resource)
					< XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION)
		wlr_xdg_surface_schedule_configure(c->surface.xdg);
}

void
monocle(Monitor *m)
{
	Client *c;
	int n = 0;

	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen || c->ismaximized)
			continue;
		resize(c, m->w, 0);
		n++;
	}
	if (n)
		snprintf(m->ltsymbol, LENGTH(m->ltsymbol), "[%d]", n);
	if ((c = focustop(m)))
		wlr_scene_node_raise_to_top(&c->scene->node);
}

/* A monitor placed at the same position as another one (wlr-randr
 * --pos) mirrors it: the scene shows the same area on both outputs. The
 * monitor that owns that point in the output layout (the older one) keeps
 * the clients and the bar; returns it for the mirror, NULL otherwise. */
Monitor *
mirrorof(Monitor *m)
{
	Monitor *o;
	if (!m->wlr_output->enabled)
		return NULL;
	o = xytomon(m->m.x, m->m.y);
	return o && o != m && o->m.x == m->m.x && o->m.y == m->m.y ? o : NULL;
}

void
motionabsolute(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits an _absolute_
	 * motion event, from 0..1 on each axis. This happens, for example, when
	 * wlroots is running under a Wayland window rather than KMS+DRM, and you
	 * move the mouse over the window. You could enter the window from any edge,
	 * so we have to warp the mouse there. Also, some hardware emits these events. */
	struct wlr_pointer_motion_absolute_event *event = data;
	double lx, ly, dx, dy;

	if (!event->time_msec) /* this is 0 with virtual pointers */
		wlr_cursor_warp_absolute(cursor, &event->pointer->base, event->x, event->y);

	wlr_cursor_absolute_to_layout_coords(cursor, &event->pointer->base, event->x, event->y, &lx, &ly);
	dx = lx - cursor->x;
	dy = ly - cursor->y;
	motionnotify(event->time_msec, &event->pointer->base, dx, dy, dx, dy);
}

void
motionnotify(uint32_t time, struct wlr_input_device *device, double dx, double dy,
		double dx_unaccel, double dy_unaccel)
{
	int nx, ny;
	double sx = 0, sy = 0, sx_confined, sy_confined;
	Client *c = NULL, *w = NULL;
	LayerSurface *l = NULL, *focusedl = NULL;
	struct wlr_surface *surface = NULL;
	struct wlr_pointer_constraint_v1 *constraint;

	/* Find the client under the pointer and send the event along. */
	xytonode(cursor->x, cursor->y, &surface, &c, &l, &sx, &sy);

	if (cursor_mode == CurPressed && !seat->drag
			&& surface != seat->pointer_state.focused_surface
			&& toplevel_from_wlr_surface(seat->pointer_state.focused_surface, &w, &focusedl) >= 0) {
		c = w;
		surface = seat->pointer_state.focused_surface;
		sx = cursor->x - (focusedl ? focusedl->scene->node.x : w->geom.x);
		sy = cursor->y - (focusedl ? focusedl->scene->node.y : w->geom.y);
	}

	/* time is 0 in internal calls meant to restore pointer focus. */
	if (time) {
		wlr_relative_pointer_manager_v1_send_relative_motion(
				relative_pointer_mgr, seat, (uint64_t)time * 1000,
				dx, dy, dx_unaccel, dy_unaccel);

		wl_list_for_each(constraint, &pointer_constraints->constraints, link)
			cursorconstrain(constraint);

		if (active_constraint && cursor_mode != CurResize && cursor_mode != CurMove) {
			toplevel_from_wlr_surface(active_constraint->surface, &c, NULL);
			if (c && active_constraint->surface == seat->pointer_state.focused_surface) {
				sx = cursor->x - c->geom.x - CONTENTX(c);
				sy = cursor->y - c->geom.y - CONTENTY(c);
				if (wlr_region_confine(&active_constraint->region, sx, sy,
						sx + dx, sy + dy, &sx_confined, &sy_confined)) {
					dx = sx_confined - sx;
					dy = sy_confined - sy;
				}

				if (active_constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
					return;
			}
		}

		wlr_cursor_move(cursor, device, dx, dy);
		wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);

		/* Update selmon (even while dragging a window) */
		if (sloppyfocus)
			selmon = xytomon(cursor->x, cursor->y);
	}

	/* Update drag icon's position */
	wlr_scene_node_set_position(&drag_icon->node, (int)round(cursor->x), (int)round(cursor->y));

	/* If we are currently grabbing the mouse, handle and return */
	if (cursor_mode == CurMove) {
		/* Move the grabbed client to the new position. */
		nx = (int)round(cursor->x) - grabcx;
		ny = (int)round(cursor->y) - grabcy;
		if (abs(selmon->w.x - nx) < (int)snap)
			nx = selmon->w.x;
		else if (abs((selmon->w.x + selmon->w.width) - (nx + grabc->geom.width)) < (int)snap)
			nx = selmon->w.x + selmon->w.width - grabc->geom.width;
		if (abs(selmon->w.y - ny) < (int)snap)
			ny = selmon->w.y;
		else if (abs((selmon->w.y + selmon->w.height) - (ny + grabc->geom.height)) < (int)snap)
			ny = selmon->w.y + selmon->w.height - grabc->geom.height;
		resize(grabc, (struct wlr_box){.x = nx, .y = ny,
			.width = grabc->geom.width, .height = grabc->geom.height}, 1);
		return;
	} else if (cursor_mode == CurResize) {
		resize(grabc, (struct wlr_box){.x = grabc->geom.x, .y = grabc->geom.y,
			.width = (int)round(cursor->x) - grabc->geom.x, .height = (int)round(cursor->y) - grabc->geom.y}, 1);
		return;
	} else if (cursor_mode == CurSwap) {
		return;
	} else if (cursor_mode == CurTileResize) {
		/* Re-tile at most ~60 times per second while dragging */
		if (time - lastresize >= 16) {
			fairresize(grabc, (int)round(cursor->x) - grabcx,
					(int)round(cursor->y) - grabcy);
			lastresize = time;
		}
		return;
	}

	/* If there's no client surface under the cursor, set the cursor image to a
	 * default. This is what makes the cursor image appear when you move it
	 * off of a client or over its border. */
	if (!surface && !seat->drag)
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	/* title bars: hovering the traffic lights, focus follows the mouse */
	if (!surface && !l && !seat->drag) {
		int part;
		Client *d = decoat(cursor->x, cursor->y, &part);
		decohover(d, part);
		if (d)
			c = d;
	} else {
		decohover(NULL, DecoNone);
	}
	dockhover(cursor->x, cursor->y);
	updatetooltip();
	pointerfocus(c, l, surface, sx, sy, time);
}

void
motionrelative(struct wl_listener *listener, void *data)
{
	/* This event is forwarded by the cursor when a pointer emits a _relative_
	 * pointer motion event (i.e. a delta) */
	struct wlr_pointer_motion_event *event = data;
	/* The cursor doesn't move unless we tell it to. The cursor automatically
	 * handles constraining the motion to the output layout, as well as any
	 * special configuration applied for the specific input device which
	 * generated the event. You can pass NULL for the device if you want to move
	 * the cursor around without any input. */
	motionnotify(event->time_msec, &event->pointer->base, event->delta_x, event->delta_y,
			event->unaccel_dx, event->unaccel_dy);
}

void
moveresize(const Arg *arg)
{
	Client *c = NULL;
	xytonode(cursor->x, cursor->y, NULL, &c, NULL, NULL, NULL);
	grabclient(c, arg->ui);
}

/* Start moving (CurMove) or resizing (CurResize) c with the pointer */
void
grabclient(Client *c, unsigned int mode)
{
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	grabc = c;
	if (!grabc || client_is_unmanaged(grabc) || grabc->isfullscreen
			|| grabc->ismaximized) {
		grabc = NULL;
		return;
	}

	if (mode == CurMove && !grabc->isfloating) {
		/* Tiled: swap with the client it is dropped on, see swapdrop() */
		cursor_mode = CurSwap;
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "grabbing");
		return;
	}
	if (mode == CurResize && !grabc->isfloating) {
		/* Tiled: change the fair layout weights, see fairresize() */
		fairbegin(grabc);
		grabcx = (int)round(cursor->x);
		grabcy = (int)round(cursor->y);
		cursor_mode = CurTileResize;
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "se-resize");
		return;
	}

	/* Float the window and tell motionnotify to grab it */
	setfloating(grabc, 1);
	switch (cursor_mode = mode) {
	case CurMove:
		grabcx = (int)round(cursor->x) - grabc->geom.x;
		grabcy = (int)round(cursor->y) - grabc->geom.y;
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "all-scroll");
		break;
	case CurResize:
		/* Doesn't work for X11 output - the next absolute motion event
		 * returns the cursor to where it started */
		wlr_cursor_warp_closest(cursor, NULL,
				grabc->geom.x + grabc->geom.width,
				grabc->geom.y + grabc->geom.height);
		wlr_cursor_set_xcursor(cursor, cursor_mgr, "se-resize");
		break;
	}
}

void
outputmgrapply(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, 0);
}

void
outputmgrapplyortest(struct wlr_output_configuration_v1 *config, int test)
{
	/*
	 * Called when a client such as wlr-randr requests a change in output
	 * configuration. This is only one way that the layout can be changed,
	 * so any Monitor information should be updated by updatemons() after an
	 * output_layout.change event, not here.
	 */
	struct wlr_output_configuration_head_v1 *config_head;
	size_t states_len, i;
	struct wlr_output_swapchain_manager swapchain_manager;
	struct wlr_backend_output_state *states =
		wlr_output_configuration_v1_build_state(config, &states_len);
	int ok = 0;

	if (!states) {
		wlr_output_configuration_v1_send_failed(config);
		return;
	}

	wlr_output_swapchain_manager_init(&swapchain_manager, backend);

	ok = wlr_output_swapchain_manager_prepare(&swapchain_manager, states, states_len);
	if (!ok || test)
		goto out;

	for (i = 0; i < states_len; i++) {
		struct wlr_swapchain *swapchain = wlr_output_swapchain_manager_get_swapchain(
				&swapchain_manager, states[i].output);
		if (swapchain && !states[i].output->enabled)
			wlr_output_state_set_buffer(&states[i].base, wlr_swapchain_acquire(swapchain));
	}

	if (!(ok = wlr_backend_commit(backend, states, states_len)))
		goto out;

	wlr_output_swapchain_manager_apply(&swapchain_manager);

	wl_list_for_each(config_head, &config->heads, link) {
		struct wlr_output *wlr_output = config_head->state.output;
		Monitor *m = wlr_output->data;

		/* Ensure displays previously disabled by wlr-output-power-management-v1
		 * are properly handled*/
		m->asleep = 0;

		/* Don't move monitors if position wouldn't change. This avoids
		 * wlroots marking the output as manually configured.
		 * wlr_output_layout_add does not like disabled outputs */
		if (config_head->state.enabled
				&& (m->m.x != config_head->state.x || m->m.y != config_head->state.y
					|| !wlr_output_layout_get(output_layout, wlr_output))) {
			wlr_output_layout_add(output_layout, wlr_output,
					config_head->state.x, config_head->state.y);
		}
	}

out:
	wlr_output_swapchain_manager_finish(&swapchain_manager);
	for (i = 0; i < states_len; i++)
		wlr_output_state_finish(&states[i].base);

	free(states);

	if (ok)
		wlr_output_configuration_v1_send_succeeded(config);
	else
		wlr_output_configuration_v1_send_failed(config);
	wlr_output_configuration_v1_destroy(config);

	/* https://codeberg.org/dwl/dwl/issues/577 */
	updatemons(NULL, NULL);
}

void
outputmgrtest(struct wl_listener *listener, void *data)
{
	struct wlr_output_configuration_v1 *config = data;
	outputmgrapplyortest(config, 1);
}

/* Split a status line into stext (shown), segment offsets and tooltips */
void
parsestatus(const char *line)
{
	const char *p, *tip = strchr(line, '\x1e');
	size_t n = 0, len;
	int i;

	nseg = 0;
	segoff[0] = 0;
	for (p = line; *p && p != tip && n < sizeof(stext) - 1; p++) {
		if (*p == '\x1f') {
			if (nseg < MAXSEG - 1)
				segoff[++nseg] = n;
			continue;
		}
		stext[n++] = *p;
	}
	stext[n] = '\0';
	segoff[++nseg] = n;

	for (i = 0; i < MAXSEG; i++)
		tips[i][0] = '\0';
	for (i = 0, p = tip; p && i < nseg; i++) {
		p++; /* skip \x1e or \x1f */
		len = strcspn(p, "\x1f");
		snprintf(tips[i], sizeof(tips[i]), "%.*s", (int)len, p);
		p = p[len] ? p + len : NULL;
	}
}

void
pointerfocus(Client *c, LayerSurface *l, struct wlr_surface *surface, double sx, double sy,
		uint32_t time)
{
	struct timespec now;

	if (surface != seat->pointer_state.focused_surface && sloppyfocus && time) {
		if (c && (!client_is_unmanaged(c) || client_wants_focus(c))) {
			focusclient(c, 0);
		} else if (l && l->layer_surface->current.keyboard_interactive) {
			focusclient(NULL, 0);
			client_notify_enter(l->layer_surface->surface, wlr_seat_get_keyboard(seat));
		}
	}

	/* If surface is NULL, clear pointer focus */
	if (!surface) {
		wlr_seat_pointer_notify_clear_focus(seat);
		return;
	}

	if (!time) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		time = now.tv_sec * 1000 + now.tv_nsec / 1000000;
	}

	/* Let the client know that the mouse cursor has entered one
	 * of its surfaces, and make keyboard focus follow if desired.
	 * wlroots makes this a no-op if surface is already focused */
	wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
	wlr_seat_pointer_notify_motion(seat, time, sx, sy);
}

void
powermgrsetmode(struct wl_listener *listener, void *data)
{
	struct wlr_output_power_v1_set_mode_event *event = data;
	struct wlr_output_state state = {0};
	Monitor *m = event->output->data;

	if (!m)
		return;

	m->gamma_lut_changed = 1; /* Reapply gamma LUT when re-enabling the output */
	wlr_output_state_set_enabled(&state, event->mode);
	wlr_output_commit_state(m->wlr_output, &state);

	m->asleep = !event->mode;
	updatemons(NULL, NULL);
}

void
quit(const Arg *arg)
{
	wl_display_terminate(dpy);
}

void
rendermon(struct wl_listener *listener, void *data)
{
	/* This function is called every time an output is ready to display a frame,
	 * generally at the output's refresh rate (e.g. 60Hz). */
	Monitor *m = wl_container_of(listener, m, frame);
	struct timespec now;
	Client *c;
	int64_t ms = nowms();
	int animating = 0;

	/* Step the animations on this monitor; frames stop when they end */
	wl_list_for_each(c, &clients, link)
		if (c->animstart && c->mon == m)
			animating |= animate(c, ms);
	if (animating)
		wlr_output_schedule_frame(m->wlr_output);

	if (!wlr_scene_output_needs_frame(m->scene_output)) {
		return;
	}

	wlr_scene_output_commit(m->scene_output, NULL);
	clock_gettime(CLOCK_MONOTONIC, &now);
	wlr_scene_output_send_frame_done(m->scene_output, &now);
}

/* Ease-out cubic step of c's slide and fade; returns 1 while it runs */
int
animate(Client *c, int64_t now)
{
	float t = MIN(1.0f, (float)(now - c->animstart) / (float)animation_ms);
	float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);

	wlr_scene_node_set_position(&c->scene->node,
			c->ax + (int)lroundf((float)(c->geom.x - c->ax) * e),
			c->ay + (int)lroundf((float)(c->geom.y - c->ay) * e));
	if (c->fading)
		clientalpha(c, e);
	if (t < 1.0f)
		return 1;
	c->animstart = c->fading = 0;
	return 0;
}

/* Opacity of a window's content, title bar, shadow and the glass behind it */
void
clientalpha(Client *c, float alpha)
{
	const float *tc = titlebar_color[c == focused_client];
	float tbar[4] = { tc[0] * alpha, tc[1] * alpha, tc[2] * alpha, tc[3] * alpha };

	c->alpha = alpha;
	wlr_scene_node_for_each_buffer(&c->scene->node, setopacity, &alpha);
	wlr_scene_rect_set_color(c->tbar, tbar);
	shadowupdate(c);
	wlr_scene_blur_set_alpha(c->blur, alpha);
}

/* macOS: a large soft shadow below the focused window, a lighter one below
 * the others. Not drawn behind the window (translucent windows show glass). */
void
shadowupdate(Client *c)
{
	int f = c == focused_client, outer = cornerradius(c) ? cornerradius(c) + (int)c->bw : 0;
	int sigma = f ? shadow_sigma : shadow_sigma_unfocused, dy = f ? shadow_dy : shadow_dy / 2;
	float a = (f ? shadow_color[3] : shadow_alpha_unfocused) * c->alpha;
	float color[4] = { shadow_color[0] * a, shadow_color[1] * a, shadow_color[2] * a, a };

	wlr_scene_node_set_enabled(&c->shadow->node, sigma && !c->isfullscreen);
	wlr_scene_shadow_set_blur_sigma(c->shadow, (float)sigma);
	wlr_scene_shadow_set_color(c->shadow, color);
	/* scenefx shadows a box inset by sigma, blurred by sigma / 2 */
	wlr_scene_node_set_position(&c->shadow->node, -sigma, -sigma + dy);
	wlr_scene_shadow_set_size(c->shadow, c->geom.width + 2 * sigma,
			c->geom.height + 2 * sigma);
	wlr_scene_shadow_set_corner_radius(c->shadow, outer);
	wlr_scene_shadow_set_clipped_region(c->shadow, (struct clipped_region){
			.area = { sigma, sigma - dy, c->geom.width, c->geom.height },
			.corners = corner_radii_all(outer) });
}

int64_t
nowms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

void
setopacity(struct wlr_scene_buffer *buffer, int sx, int sy, void *data)
{
	wlr_scene_buffer_set_opacity(buffer, *(float *)data);
}

void
requestdecorationmode(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_decoration_mode);
	if (c->surface.xdg->initialized)
		wlr_xdg_toplevel_decoration_v1_set_mode(c->decoration,
				WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

void
requeststartdrag(struct wl_listener *listener, void *data)
{
	struct wlr_seat_request_start_drag_event *event = data;

	if (wlr_seat_validate_pointer_grab_serial(seat, event->origin,
			event->serial))
		wlr_seat_start_pointer_drag(seat, event->drag, event->serial);
	else
		wlr_data_source_destroy(event->drag->source);
}

void
requestmonstate(struct wl_listener *listener, void *data)
{
	struct wlr_output_event_request_state *event = data;
	wlr_output_commit_state(event->output, event->state);
	updatemons(NULL, NULL);
}

void
resize(Client *c, struct wlr_box geo, int interact)
{
	struct wlr_box *bbox;
	struct wlr_box clip, old;
	int radius, outer;

	if (!c->mon || !client_surface(c)->mapped)
		return;

	bbox = interact ? &sgeom : &c->mon->w;

	client_set_bounds(c, geo.width, geo.height);
	old = c->geom;
	c->geom = geo;
	applybounds(c, bbox);

	radius = cornerradius(c);
	outer = radius ? radius + (int)c->bw : 0;

	/* Update scene-graph, including borders. The border is a rectangle with
	 * a hole for the content, so their rounded corners stay concentric.
	 * A new window fades in where it is placed, layout moves slide there
	 * (rendermon() steps both); drags and fullscreen are immediate. A hidden
	 * window (setmon() places a new one before arrange() shows it) is not
	 * placed yet: it fades in at its first place on screen. */
	if (!c->scene->node.enabled) {
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
	} else if (!animation_ms || interact || c->isfullscreen) {
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
		if (c->fading)
			clientalpha(c, 1.0f);
		c->animstart = c->fading = 0;
	} else if (!c->placed) {
		wlr_scene_node_set_position(&c->scene->node, c->geom.x, c->geom.y);
		c->ax = c->geom.x;
		c->ay = c->geom.y;
		c->fading = 1;
		c->animstart = nowms();
		clientalpha(c, 0.0f);
	} else if (old.x != c->geom.x || old.y != c->geom.y) {
		c->ax = c->scene->node.x;
		c->ay = c->scene->node.y;
		c->animstart = nowms();
	}
	c->placed |= c->scene->node.enabled;
	if (c->animstart)
		wlr_output_schedule_frame(c->mon->wlr_output);
	wlr_scene_node_set_position(&c->scene_surface->node, CONTENTX(c), CONTENTY(c));
	wlr_scene_rect_set_size(c->border, c->geom.width, c->geom.height);
	wlr_scene_rect_set_corner_radius(c->border, outer);
	wlr_scene_rect_set_clipped_region(c->border, (struct clipped_region){
			.area = { c->bw, c->bw, c->geom.width - 2 * c->bw, c->geom.height - 2 * c->bw },
			.corners = corner_radii_all(radius) });

	shadowupdate(c);

	/* The fullscreen background hides what's behind fullscreen windows */
	wlr_scene_node_set_enabled(&c->blur->node, blur_data.num_passes && !c->isfullscreen);
	wlr_scene_node_set_position(&c->blur->node, c->bw, c->bw);
	wlr_scene_blur_set_size(c->blur, c->geom.width - 2 * c->bw, c->geom.height - 2 * c->bw);
	wlr_scene_blur_set_corner_radius(c->blur, radius);
	/* All windows use the cached blur of the background layer: blurring a
	 * window that is already blurred again makes it almost invisible */
	wlr_scene_blur_set_should_only_blur_bottom_layer(c->blur, 1);

	decoupdate(c);
	client_set_size(c, CONTENTW(c), CONTENTH(c));
	client_get_clip(c, &clip);
	wlr_scene_subsurface_tree_set_clip(&c->scene_surface->node, &clip);
	wlr_scene_node_for_each_buffer(&c->scene_surface->node, roundbuffer, c);
}

/* Keyboard resize by resizestep pixels: tiled clients move the fair
 * boundary, floating clients change size */
void
resizedir(const Arg *arg)
{
	Client *sel = focustop(selmon);
	int dx = arg->i == WLR_DIRECTION_LEFT ? -resizestep
		: arg->i == WLR_DIRECTION_RIGHT ? resizestep : 0;
	int dy = arg->i == WLR_DIRECTION_UP ? -resizestep
		: arg->i == WLR_DIRECTION_DOWN ? resizestep : 0;

	if (!sel || sel->isfullscreen)
		return;
	if (sel->isfloating) {
		resize(sel, (struct wlr_box){.x = sel->geom.x, .y = sel->geom.y,
			.width = sel->geom.width + dx, .height = sel->geom.height + dy}, 1);
		return;
	}
	fairbegin(sel);
	fairresize(sel, dx, dy);
}

void
roundbuffer(struct wlr_scene_buffer *buffer, int sx, int sy, void *data)
{
	/* Round the corners of buffers which are at the corners of the window,
	 * including subsurfaces covering it */
	Client *c = data;
	int radius = cornerradius(c);
	struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(buffer);
	/* the window's own surface is always rounded: its size can lag behind
	 * the window's while it resizes */
	int main = ss && ss->surface == client_surface(c);
	int left = main || sx <= CONTENTX(c), top = (main || sy <= CONTENTY(c)) && !c->th;
	int right = main || sx + buffer->dst_width >= CONTENTX(c) + CONTENTW(c);
	int bottom = main || sy + buffer->dst_height >= CONTENTY(c) + CONTENTH(c);

	wlr_scene_buffer_set_corner_radii(buffer, corner_radii_new(
			top && left ? radius : 0, top && right ? radius : 0,
			bottom && right ? radius : 0, bottom && left ? radius : 0));
}

void
roundlayer(struct wlr_scene_buffer *buffer, int sx, int sy, void *data)
{
	/* Match the rounded glass behind layer surfaces */
	wlr_scene_buffer_set_corner_radii(buffer, corner_radii_all(corner_radius));
}

struct wlr_scene_blur *
glassbelow(struct wlr_scene_node *node)
{
	/* Liquid glass right below node, which must be translucent to show it */
	struct wlr_scene_blur *blur = wlr_scene_blur_create(node->parent, 0, 0);
	wlr_scene_node_place_below(&blur->node, node);
	wlr_scene_blur_set_glass(blur, 1);
	/* Blurring blurred windows again would make them almost invisible */
	wlr_scene_blur_set_should_only_blur_bottom_layer(blur, 1);
	return blur;
}

void
run(char *startup_cmd)
{
	/* Add a Unix socket to the Wayland display. */
	const char *socket = wl_display_add_socket_auto(dpy);
	if (!socket)
		die("startup: display_add_socket_auto");
	setenv("WAYLAND_DISPLAY", socket, 1);

	/* Start the backend. This will enumerate outputs and inputs, become the DRM
	 * master, etc */
	if (!wlr_backend_start(backend))
		die("startup: backend_start");

	/* Now that the socket exists and the backend is started, run the startup command */
	if (startup_cmd) {
		if ((child_pid = fork()) < 0)
			die("startup: fork:");
		if (child_pid == 0) {
			close(STDIN_FILENO);
			setsid();
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, NULL);
			die("startup: execl:");
		}
	}

	/* Mark stdout as non-blocking to avoid the startup script
	 * causing dwl to freeze when a user neither closes stdin
	 * nor consumes standard input in his startup script */

	if (fd_set_nonblock(STDOUT_FILENO) < 0)
		close(STDOUT_FILENO);

	drawbars();

	/* At this point the outputs are initialized, choose initial selmon based on
	 * cursor position, and set default cursor image */
	selmon = xytomon(cursor->x, cursor->y);

	/* TODO hack to get cursor to display in its initial location (100, 100)
	 * instead of (0, 0) and then jumping. Still may not be fully
	 * initialized, as the image/coordinates are not transformed for the
	 * monitor when displayed here */
	wlr_cursor_warp_closest(cursor, NULL, cursor->x, cursor->y);
	wlr_cursor_set_xcursor(cursor, cursor_mgr, "default");

	/* Run the Wayland event loop. This does not return until you exit the
	 * compositor. Starting the backend rigged up all of the necessary event
	 * loop configuration to listen to libinput events, DRM events, generate
	 * frame events at the refresh rate, and so on. */
	wl_display_run(dpy);
}

void
setcursor(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client provides a cursor image */
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	/* If we're "grabbing" the cursor, don't use the client's image, we will
	 * restore it after "grabbing" sending a leave event, followed by a enter
	 * event, which will result in the client requesting set the cursor surface */
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	/* This can be sent by any client, so we check to make sure this one
	 * actually has pointer focus first. If so, we can tell the cursor to
	 * use the provided surface as the cursor image. It will set the
	 * hardware cursor on the output that it's currently on and continue to
	 * do so as the cursor moves between outputs. */
	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_surface(cursor, event->surface,
				event->hotspot_x, event->hotspot_y);
}

void
setcursorshape(struct wl_listener *listener, void *data)
{
	struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
	if (cursor_mode != CurNormal && cursor_mode != CurPressed)
		return;
	/* This can be sent by any client, so we check to make sure this one
	 * actually has pointer focus first. If so, we can tell the cursor to
	 * use the provided cursor shape. */
	if (event->seat_client == seat->pointer_state.focused_client)
		wlr_cursor_set_xcursor(cursor, cursor_mgr,
				wlr_cursor_shape_v1_name(event->shape));
}

void
setfloating(Client *c, int floating)
{
	Client *p = client_get_parent(c);
	c->isfloating = floating;
	/* If in floating layout do not change the client's layer */
	if (!c->mon || !client_surface(c)->mapped || !c->mon->lt[c->mon->sellt]->arrange)
		return;
	wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen ||
			(p && p->isfullscreen) ? LyrFS
			: c->isfloating ? LyrFloat : LyrTile]);
	arrange(c->mon);
	drawbars();
}

void
setfullscreen(Client *c, int fullscreen)
{
	c->isfullscreen = fullscreen;
	if (!c->mon || !client_surface(c)->mapped)
		return;
	c->bw = fullscreen ? 0 : borderpx;
	c->th = fullscreen || !c->decoration ? 0 : titlebar_height;
	client_set_fullscreen(c, fullscreen);
	wlr_scene_node_reparent(&c->scene->node, layers[c->isfullscreen
			? LyrFS : c->isfloating ? LyrFloat : LyrTile]);

	if (fullscreen) {
		c->prev = c->geom;
		resize(c, c->mon->m, 0);
	} else {
		/* restore previous size instead of arrange for floating windows since
		 * client positions are set by the user and cannot be recalculated */
		resize(c, c->prev, 0);
	}
	arrange(c->mon);
	drawbars();
}

void
setlayout(const Arg *arg)
{
	if (!selmon)
		return;
	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
		selmon->sellt ^= 1;
	if (arg && arg->v)
		selmon->lt[selmon->sellt] = (Layout *)arg->v;
	strncpy(selmon->ltsymbol, selmon->lt[selmon->sellt]->symbol, sizeof(selmon->ltsymbol));
	arrange(selmon);
	drawbar(selmon);
}

/* arg > 1.0 will set mfact absolutely */
void
setmfact(const Arg *arg)
{
	float f;

	if (!arg || !selmon || !selmon->lt[selmon->sellt]->arrange)
		return;
	f = arg->f < 1.0f ? arg->f + selmon->mfact : arg->f - 1.0f;
	if (f < 0.1 || f > 0.9)
		return;
	selmon->mfact = f;
	arrange(selmon);
}

void
setmon(Client *c, Monitor *m, uint32_t newtags)
{
	Monitor *oldmon = c->mon;

	if (oldmon == m)
		return;
	c->mon = m;
	c->prev = c->geom;

	/* Scene graph sends surface leave/enter events on move and resize */
	if (oldmon)
		arrange(oldmon);
	if (m) {
		/* Make sure window actually overlaps with the monitor */
		resize(c, c->geom, 0);
		c->tags = newtags ? newtags : m->tagset[m->seltags]; /* assign tags of target monitor */
		setfullscreen(c, c->isfullscreen); /* This will call arrange(c->mon) */
		setfloating(c, c->isfloating);
	}
	focusclient(focustop(selmon), 1);
}

void
setpsel(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in dwl we always honor them
	 */
	struct wlr_seat_request_set_primary_selection_event *event = data;
	wlr_seat_set_primary_selection(seat, event->source, event->serial);
}

void
setsel(struct wl_listener *listener, void *data)
{
	/* This event is raised by the seat when a client wants to set the selection,
	 * usually when the user copies something. wlroots allows compositors to
	 * ignore such requests if they so choose, but in dwl we always honor them
	 */
	struct wlr_seat_request_set_selection_event *event = data;
	wlr_seat_set_selection(seat, event->source, event->serial);
}

void
setup(void)
{
	int drm_fd, i, sig[] = {SIGCHLD, SIGINT, SIGTERM, SIGPIPE};
	struct sigaction sa = {.sa_flags = SA_RESTART, .sa_handler = handlesig};
	sigemptyset(&sa.sa_mask);

	for (i = 0; i < (int)LENGTH(sig); i++)
		sigaction(sig[i], &sa, NULL);


	wlr_log_init(log_level, NULL);

	/* The Wayland display is managed by libwayland. It handles accepting
	 * clients from the Unix socket, managing Wayland globals, and so on. */
	dpy = wl_display_create();
	event_loop = wl_display_get_event_loop(dpy);

	/* The backend is a wlroots feature which abstracts the underlying input and
	 * output hardware. The autocreate option will choose the most suitable
	 * backend based on the current environment, such as opening an X11 window
	 * if an X11 server is running. */
	if (!(backend = wlr_backend_autocreate(event_loop, &session)))
		die("couldn't create backend");

	/* Initialize the scene graph used to lay out windows */
	scene = wlr_scene_create();
	wlr_scene_set_blur_data(scene, blur_data.num_passes, (int)blur_data.radius, blur_data.noise,
			blur_data.brightness, blur_data.contrast, blur_data.saturation);
	wlr_scene_set_glass_data(scene, glass_data);
	root_bg = wlr_scene_rect_create(&scene->tree, 0, 0, rootcolor);
	for (i = 0; i < NUM_LAYERS; i++)
		layers[i] = wlr_scene_tree_create(&scene->tree);
	drag_icon = wlr_scene_tree_create(&scene->tree);
	wlr_scene_node_place_below(&drag_icon->node, &layers[LyrBlock]->node);

	/* Creates scenefx's GLES2 renderer, which is required to render its effects.
	 * The renderer is responsible for defining the various pixel formats it
	 * supports for shared memory, this configures that for clients. */
	if (!(drw = fx_renderer_create(backend)))
		die("couldn't create renderer");
	wl_signal_add(&drw->events.lost, &gpu_reset);

	/* Create shm, drm and linux_dmabuf interfaces by ourselves.
	 * The simplest way is to call:
	 *      wlr_renderer_init_wl_display(drw);
	 * but we need to create the linux_dmabuf interface manually to integrate it
	 * with wlr_scene. */
	wlr_renderer_init_wl_shm(drw, dpy);

	if (wlr_renderer_get_texture_formats(drw, WLR_BUFFER_CAP_DMABUF)
			&& wlr_renderer_get_drm_fd(drw) >= 0) {
		wlr_drm_create(dpy, drw);
		wlr_scene_set_linux_dmabuf_v1(scene,
				wlr_linux_dmabuf_v1_create_with_renderer(dpy, 5, drw));
	}

	if ((drm_fd = wlr_renderer_get_drm_fd(drw)) >= 0 && drw->features.timeline
			&& backend->features.timeline)
		wlr_linux_drm_syncobj_manager_v1_create(dpy, 1, drm_fd);

	/* Autocreates an allocator for us.
	 * The allocator is the bridge between the renderer and the backend. It
	 * handles the buffer creation, allowing wlroots to render onto the
	 * screen */
	if (!(alloc = wlr_allocator_autocreate(backend, drw)))
		die("couldn't create allocator");

	/* This creates some hands-off wlroots interfaces. The compositor is
	 * necessary for clients to allocate surfaces and the data device manager
	 * handles the clipboard. Each of these wlroots interfaces has room for you
	 * to dig your fingers in and play with their behavior if you want. Note that
	 * the clients cannot set the selection directly without compositor approval,
	 * see the setsel() function. */
	compositor = wlr_compositor_create(dpy, 6, drw);
	wlr_subcompositor_create(dpy);
	wlr_data_device_manager_create(dpy);
	wlr_export_dmabuf_manager_v1_create(dpy);
	wlr_screencopy_manager_v1_create(dpy);
	wlr_ext_image_copy_capture_manager_v1_create(dpy, 1);
	wlr_ext_output_image_capture_source_manager_v1_create(dpy, 1);
	wlr_data_control_manager_v1_create(dpy);
	wlr_ext_data_control_manager_v1_create(dpy, 1);
	wlr_primary_selection_v1_device_manager_create(dpy);
	wlr_viewporter_create(dpy);
	wlr_single_pixel_buffer_manager_v1_create(dpy);
	wlr_fractional_scale_manager_v1_create(dpy, 1);
	wlr_presentation_create(dpy, backend, 2);
	wlr_alpha_modifier_v1_create(dpy);

	/* Initializes the interface used to implement urgency hints */
	activation = wlr_xdg_activation_v1_create(dpy);
	wl_signal_add(&activation->events.request_activate, &request_activate);

	wlr_scene_set_gamma_control_manager_v1(scene, wlr_gamma_control_manager_v1_create(dpy));

	power_mgr = wlr_output_power_manager_v1_create(dpy);
	wl_signal_add(&power_mgr->events.set_mode, &output_power_mgr_set_mode);

	/* Creates an output layout, which is a wlroots utility for working with an
	 * arrangement of screens in a physical layout. */
	output_layout = wlr_output_layout_create(dpy);
	wl_signal_add(&output_layout->events.change, &layout_change);

    wlr_xdg_output_manager_v1_create(dpy, output_layout);

	/* Configure a listener to be notified when new outputs are available on the
	 * backend. */
	wl_list_init(&mons);
	wl_signal_add(&backend->events.new_output, &new_output);

	/* Set up our client lists, the xdg-shell and the layer-shell. The xdg-shell is a
	 * Wayland protocol which is used for application windows. For more
	 * detail on shells, refer to the article:
	 *
	 * https://drewdevault.com/2018/07/29/Wayland-shells.html
	 */
	wl_list_init(&clients);
	wl_list_init(&fstack);

	xdg_shell = wlr_xdg_shell_create(dpy, 6);
	wl_signal_add(&xdg_shell->events.new_toplevel, &new_xdg_toplevel);
	wl_signal_add(&xdg_shell->events.new_popup, &new_xdg_popup);

	layer_shell = wlr_layer_shell_v1_create(dpy, 4);
	wl_signal_add(&layer_shell->events.new_surface, &new_layer_surface);

	idle_notifier = wlr_idle_notifier_v1_create(dpy);

	idle_inhibit_mgr = wlr_idle_inhibit_v1_create(dpy);
	wl_signal_add(&idle_inhibit_mgr->events.new_inhibitor, &new_idle_inhibitor);

	session_lock_mgr = wlr_session_lock_manager_v1_create(dpy);
	wl_signal_add(&session_lock_mgr->events.new_lock, &new_session_lock);
	locked_bg = wlr_scene_rect_create(layers[LyrBlock], sgeom.width, sgeom.height,
			(float [4]){0.1f, 0.1f, 0.1f, 1.0f});
	wlr_scene_node_set_enabled(&locked_bg->node, 0);

	foreign_toplevel_list = wlr_ext_foreign_toplevel_list_v1_create(dpy,1);
	ext_foreign_toplevel_image_capture_source_manager_v1 =
            wlr_ext_foreign_toplevel_image_capture_source_manager_v1_create(dpy, 1);
	wl_signal_add(&ext_foreign_toplevel_image_capture_source_manager_v1->events.new_request,
			&new_foreign_toplevel_capture_request);

	/* Use decoration protocols to negotiate server-side decorations */
	wlr_server_decoration_manager_set_default_mode(
			wlr_server_decoration_manager_create(dpy),
			WLR_SERVER_DECORATION_MANAGER_MODE_SERVER);
	xdg_decoration_mgr = wlr_xdg_decoration_manager_v1_create(dpy);
	wl_signal_add(&xdg_decoration_mgr->events.new_toplevel_decoration, &new_xdg_decoration);

	pointer_constraints = wlr_pointer_constraints_v1_create(dpy);
	wl_signal_add(&pointer_constraints->events.new_constraint, &new_pointer_constraint);

	relative_pointer_mgr = wlr_relative_pointer_manager_v1_create(dpy);

	/*
	 * Creates a cursor, which is a wlroots utility for tracking the cursor
	 * image shown on screen.
	 */
	cursor = wlr_cursor_create();
	wlr_cursor_attach_output_layout(cursor, output_layout);

	/* Creates an xcursor manager, another wlroots utility which loads up
	 * Xcursor themes to source cursor images from and makes sure that cursor
	 * images are available at all scale factors on the screen (necessary for
	 * HiDPI support). Scaled cursors will be loaded with each output. */
	cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
	setenv("XCURSOR_SIZE", "24", 1);

	/*
	 * wlr_cursor *only* displays an image on screen. It does not move around
	 * when the pointer moves. However, we can attach input devices to it, and
	 * it will generate aggregate events for all of them. In these events, we
	 * can choose how we want to process them, forwarding them to clients and
	 * moving the cursor around. More detail on this process is described in
	 * https://drewdevault.com/2018/07/17/Input-handling-in-wlroots.html
	 *
	 * And more comments are sprinkled throughout the notify functions above.
	 */
	wl_signal_add(&cursor->events.motion, &cursor_motion);
	wl_signal_add(&cursor->events.motion_absolute, &cursor_motion_absolute);
	wl_signal_add(&cursor->events.button, &cursor_button);
	wl_signal_add(&cursor->events.axis, &cursor_axis);
	wl_signal_add(&cursor->events.frame, &cursor_frame);
	wl_signal_add(&cursor->events.touch_down, &touch_down);
	wl_signal_add(&cursor->events.touch_frame, &touch_frame);
	wl_signal_add(&cursor->events.touch_motion, &touch_motion);
	wl_signal_add(&cursor->events.touch_up, &touch_up);

	cursor_shape_mgr = wlr_cursor_shape_manager_v1_create(dpy, 1);
	wl_signal_add(&cursor_shape_mgr->events.request_set_shape, &request_set_cursor_shape);

	/*
	 * Configures a seat, which is a single "seat" at which a user sits and
	 * operates the computer. This conceptually includes up to one keyboard,
	 * pointer, touch, and drawing tablet device. We also rig up a listener to
	 * let us know when new input devices are available on the backend.
	 */
	wl_signal_add(&backend->events.new_input, &new_input_device);
	virtual_keyboard_mgr = wlr_virtual_keyboard_manager_v1_create(dpy);
	wl_signal_add(&virtual_keyboard_mgr->events.new_virtual_keyboard,
			&new_virtual_keyboard);
	virtual_pointer_mgr = wlr_virtual_pointer_manager_v1_create(dpy);
    wl_signal_add(&virtual_pointer_mgr->events.new_virtual_pointer,
            &new_virtual_pointer);

	seat = wlr_seat_create(dpy, "seat0");
	wl_signal_add(&seat->events.request_set_cursor, &request_cursor);
	wl_signal_add(&seat->events.request_set_selection, &request_set_sel);
	wl_signal_add(&seat->events.request_set_primary_selection, &request_set_psel);
	wl_signal_add(&seat->events.request_start_drag, &request_start_drag);
	wl_signal_add(&seat->events.start_drag, &start_drag);

	kb_group = createkeyboardgroup();
	wl_list_init(&kb_group->destroy.link);

	output_mgr = wlr_output_manager_v1_create(dpy);
	wl_signal_add(&output_mgr->events.apply, &output_mgr_apply);
	wl_signal_add(&output_mgr->events.test, &output_mgr_test);

	input_method_manager = wlr_input_method_manager_v2_create(dpy);
	text_input_manager = wlr_text_input_manager_v3_create(dpy);
	input_method_relay = input_method_relay_create();

	drwl_init();
	tooltip = wlr_scene_buffer_create(layers[LyrOverlay], NULL);
	wlr_scene_node_set_enabled(&tooltip->node, 0);
	tooltipblur = glassbelow(&tooltip->node);
	wlr_scene_node_set_enabled(&tooltipblur->node, 0);

	status_event_source = wl_event_loop_add_fd(wl_display_get_event_loop(dpy),
		STDIN_FILENO, WL_EVENT_READABLE, statusin, NULL);

	/* Make sure XWayland clients don't connect to the parent X server,
	 * e.g when running in the x11 backend or the wayland backend and the
	 * compositor has Xwayland support */
	unsetenv("DISPLAY");
}

void
spawn(const Arg *arg)
{
	Client *c;
	char geom[64];

	if (fork() == 0) {
		close(STDIN_FILENO);
		open("/dev/null", O_RDWR);
		dup2(STDERR_FILENO, STDOUT_FILENO);
		setsid();
		/* Focused window geometry for "grim -g" (dwl has no IPC) */
		if ((c = focustop(selmon))) {
			snprintf(geom, sizeof(geom), "%d,%d %dx%d",
				c->geom.x + (int)c->bw, c->geom.y + (int)c->bw,
				c->geom.width - 2 * (int)c->bw, c->geom.height - 2 * (int)c->bw);
			setenv("DWL_FOCUSED_GEOM", geom, 1);
		}
		execvp(((char **)arg->v)[0], (char **)arg->v);
		die("dwl: execvp %s failed:", ((char **)arg->v)[0]);
	}
}

void
startdrag(struct wl_listener *listener, void *data)
{
	struct wlr_drag *drag = data;
	if (!drag->icon)
		return;

	drag->icon->data = &wlr_scene_drag_icon_create(drag_icon, drag->icon)->node;
	LISTEN_STATIC(&drag->icon->events.destroy, destroydragicon);
}

int
statusin(int fd, unsigned int mask, void *data)
{
	char status[1024], *line;
	ssize_t n;

	if (mask & WL_EVENT_ERROR)
		die("status in event error");

	if ((n = read(fd, status, sizeof(status) - 1)) <= 0) {
		/* EOF (status command exited) or error: stop reading */
		if (n == 0 || errno != EAGAIN)
			wl_event_source_remove(status_event_source);
		return 0;
	}
	status[n] = '\0';
	if (status[n - 1] == '\n')
		status[n - 1] = '\0';
	/* show the newest line if several were read at once */
	line = (line = strrchr(status, '\n')) ? line + 1 : status;

	parsestatus(line);
	drawbars();
	if (tipseg >= 0)
		drawtooltip(); /* live values while hovering */

	return 0;
}

/* Swap the positions of a and b in the client list (the tiling order) */
void
swapclients(Client *a, Client *b)
{
	struct wl_list *aprev = a->link.prev;

	if (a == b)
		return;
	wl_list_remove(&a->link);
	if (aprev == &b->link) { /* b was right before a */
		wl_list_insert(b->link.prev, &a->link);
		return;
	}
	wl_list_insert(&b->link, &a->link);
	wl_list_remove(&b->link);
	wl_list_insert(aprev, &b->link);
}

/* End of a MOD + left drag of a tiled client: swap it with the tiled
 * client under the cursor, also across monitors, or move it to the
 * monitor under the cursor */
void
swapdrop(Client *c)
{
	Client *t = NULL;
	Monitor *m;
	uint32_t ctags;

	xytonode(cursor->x, cursor->y, NULL, &t, NULL, NULL, NULL);
	if (!t) {
		setmon(c, selmon, 0);
		return;
	}
	if (t == c || t->isfloating || t->isfullscreen || client_is_unmanaged(t))
		return;
	if (t->mon != c->mon) {
		m = c->mon;
		ctags = c->tags;
		setmon(c, t->mon, t->tags);
		setmon(t, m, ctags);
	}
	swapclients(c, t);
	arrange(c->mon);
	if (t->mon != c->mon)
		arrange(t->mon);
	focusclient(c, 1);
}

void
tag(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (!sel || (arg->ui & TAGMASK) == 0)
		return;

	sel->tags = arg->ui & TAGMASK;
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	drawbars();
}

/* Move the focused client to a tag and view that tag */
void
tagfollow(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (!sel || (arg->ui & TAGMASK) == 0)
		return;
	tag(arg);
	view(arg);
}

void
tagmon(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		setmon(sel, dirtomon(arg->i), 0);
}

void
tile(Monitor *m)
{
	unsigned int mw, my, ty;
	int i, n = 0;
	Client *c;

	wl_list_for_each(c, &clients, link)
		if (VISIBLEON(c, m) && !c->isfloating && !c->isfullscreen)
			n++;
	if (n == 0)
		return;

	if (n > m->nmaster)
		mw = m->nmaster ? (int)roundf(m->w.width * m->mfact) : 0;
	else
		mw = m->w.width;
	i = my = ty = 0;
	wl_list_for_each(c, &clients, link) {
		if (!VISIBLEON(c, m) || c->isfloating || c->isfullscreen || c->ismaximized)
			continue;
		if (i < m->nmaster) {
			resize(c, (struct wlr_box){.x = m->w.x, .y = m->w.y + my, .width = mw,
				.height = (m->w.height - my) / (MIN(n, m->nmaster) - i)}, 0);
			my += c->geom.height;
		} else {
			resize(c, (struct wlr_box){.x = m->w.x + mw, .y = m->w.y + ty,
				.width = m->w.width - mw, .height = (m->w.height - ty) / (n - i)}, 0);
			ty += c->geom.height;
		}
		i++;
	}
}

void
togglebar(const Arg *arg)
{
	wlr_scene_node_set_enabled(&selmon->scene_buffer->node,
		!selmon->scene_buffer->node.enabled);
	arrangelayers(selmon);
	drawbars();
}

void
togglefloating(const Arg *arg)
{
	Client *sel = focustop(selmon);
	/* return if fullscreen */
	if (sel && !sel->isfullscreen) {
		sel->ismaximized = 0;
		setfloating(sel, !sel->isfloating);
	}
}

/* Fill the window area (the bar stays visible); tiled clients keep their
 * place in the layout, floating clients get their old geometry back */
void
togglemaximize(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (!sel || sel->isfullscreen)
		return;
	if ((sel->ismaximized = !sel->ismaximized)) {
		if (sel->isfloating)
			sel->premax = sel->geom;
	} else if (sel->isfloating) {
		resize(sel, sel->premax, 0);
	}
	arrange(selmon);
	wlr_scene_node_raise_to_top(&sel->scene->node);
}

void
togglefullscreen(const Arg *arg)
{
	Client *sel = focustop(selmon);
	if (sel)
		setfullscreen(sel, !sel->isfullscreen);
}

void
toggletag(const Arg *arg)
{
	uint32_t newtags;
	Client *sel = focustop(selmon);
	if (!sel || !(newtags = sel->tags ^ (arg->ui & TAGMASK)))
		return;

	sel->tags = newtags;
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	drawbars();
}

void
toggleview(const Arg *arg)
{
	uint32_t newtagset;
	if (!(newtagset = selmon ? selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK) : 0))
		return;

	selmon->tagset[selmon->seltags] = newtagset;
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	drawbars();
}

void
unlocksession(struct wl_listener *listener, void *data)
{
	SessionLock *lock = wl_container_of(listener, lock, unlock);
	destroylock(lock, 1);
}

void
unmaplayersurfacenotify(struct wl_listener *listener, void *data)
{
	LayerSurface *l = wl_container_of(listener, l, unmap);

	l->mapped = 0;
	wlr_scene_node_set_enabled(&l->scene->node, 0);
	if (l == exclusive_focus)
		exclusive_focus = NULL;
	if (l->layer_surface->output && (l->mon = l->layer_surface->output->data))
		arrangelayers(l->mon);
	if (l->mon && l->scene->node.parent == layers[LyrBg])
		wlr_scene_optimized_blur_mark_dirty(l->mon->blur);
	if (l->layer_surface->surface == seat->keyboard_state.focused_surface)
		focusclient(focustop(selmon), 1);
	motionnotify(0, NULL, 0, 0, 0, 0);
}

void
unmapnotify(struct wl_listener *listener, void *data)
{
	/* Called when the surface is unmapped, and should no longer be shown. */
	Client *c = wl_container_of(listener, c, unmap);
	if (c == grabc) {
		cursor_mode = CurNormal;
		grabc = NULL;
	}
	if (c == focused_client)
		focused_client = NULL;
	if (c == hoverc)
		hoverc = NULL;
	if (c == pressc)
		pressc = NULL;
	if (c == lastclickc)
		lastclickc = NULL;

	if (client_is_unmanaged(c)) {
		if (c == exclusive_focus) {
			exclusive_focus = NULL;
			focusclient(focustop(selmon), 1);
		}
	} else {
		wl_list_remove(&c->link);
		setmon(c, NULL, 0);
		wl_list_remove(&c->flink);
	}

	if (c->foreign_toplevel_handle) {
		wlr_ext_foreign_toplevel_handle_v1_destroy(c->foreign_toplevel_handle);
		c->foreign_toplevel_handle = NULL;
	}

	wlr_scene_node_destroy(&c->image_capture_scene->tree.node);
	c->image_capture_scene = NULL;
	c->image_capture_source = NULL;
	wlr_scene_node_destroy(&c->scene->node);
	c->scene = NULL;
	c->tbar = c->tsep = NULL;
	c->title = c->buttons = NULL;
	client_surface(c)->data = NULL;
	drawbars();
	dockchanged();
	motionnotify(0, NULL, 0, 0, 0, 0);
}

/* The touchscreen is the built-in panel: map 0..1 to the eDP monitor, not the
 * whole layout (wrong as soon as an external monitor is attached). */
static void
touchtolayout(double x, double y, double *lx, double *ly)
{
	Monitor *m, *t = NULL;
	wl_list_for_each(m, &mons, link)
		if (!t || !strncmp(m->wlr_output->name, "eDP", 3))
			t = m;
	if (!t) {
		wlr_cursor_absolute_to_layout_coords(cursor, NULL, x, y, lx, ly);
		return;
	}
	*lx = t->m.x + x * t->m.width;
	*ly = t->m.y + y * t->m.height;
}

/* Clients that support wl_touch get real touch events; for everything else
 * (the bar, root window, surfaces without touch) the first finger is the left
 * mouse button, so focus, bar clicks and bindings work as with the pointer. */
static void
touchpointer(uint32_t time, double lx, double ly, int press, int release)
{
	struct wlr_pointer_button_event ev = {.time_msec = time, .button = BTN_LEFT,
		.state = release ? WL_POINTER_BUTTON_STATE_RELEASED : WL_POINTER_BUTTON_STATE_PRESSED};
	wlr_cursor_warp_closest(cursor, NULL, lx, ly);
	motionnotify(time, NULL, 0, 0, 0, 0);
	if (press || release)
		buttonpress(NULL, &ev);
	wlr_seat_pointer_notify_frame(seat);
}

void
touchdown(struct wl_listener *listener, void *data)
{
	struct wlr_touch_down_event *event = data;
	struct wlr_surface *surface = NULL;
	Client *c = NULL;
	double lx, ly, sx, sy;

	wlr_idle_notifier_v1_notify_activity(idle_notifier, seat);
	touchtolayout(event->x, event->y, &lx, &ly);
	xytonode(lx, ly, &surface, &c, NULL, &sx, &sy);

	if (surface && wlr_surface_accepts_touch(surface, seat)) {
		if (!locked && c && (!client_is_unmanaged(c) || client_wants_focus(c)))
			focusclient(c, 1);
		touch_ox[event->touch_id & 15] = lx - sx;
		touch_oy[event->touch_id & 15] = ly - sy;
		wlr_seat_touch_notify_down(seat, surface, event->time_msec,
				event->touch_id, sx, sy);
	} else if (touch_emulated < 0) {
		touch_emulated = event->touch_id;
		touchpointer(event->time_msec, lx, ly, 1, 0);
	}
}

void
touchmotion(struct wl_listener *listener, void *data)
{
	struct wlr_touch_motion_event *event = data;
	double lx, ly;

	touchtolayout(event->x, event->y, &lx, &ly);
	if (wlr_seat_touch_get_point(seat, event->touch_id))
		wlr_seat_touch_notify_motion(seat, event->time_msec, event->touch_id,
				lx - touch_ox[event->touch_id & 15], ly - touch_oy[event->touch_id & 15]);
	else if (event->touch_id == touch_emulated)
		touchpointer(event->time_msec, lx, ly, 0, 0);
}

void
touchup(struct wl_listener *listener, void *data)
{
	struct wlr_touch_up_event *event = data;

	if (wlr_seat_touch_get_point(seat, event->touch_id)) {
		wlr_seat_touch_notify_up(seat, event->time_msec, event->touch_id);
	} else if (event->touch_id == touch_emulated) {
		touch_emulated = -1;
		touchpointer(event->time_msec, cursor->x, cursor->y, 0, 1);
	}
}

void
touchframe(struct wl_listener *listener, void *data)
{
	wlr_seat_touch_notify_frame(seat);
}

void
updatemons(struct wl_listener *listener, void *data)
{
	/*
	 * Called whenever the output layout changes: adding or removing a
	 * monitor, changing an output's mode or position, etc. This is where
	 * the change officially happens and we update geometry, window
	 * positions, focus, and the stored configuration in wlroots'
	 * output-manager implementation.
	 */
	struct wlr_output_configuration_v1 *config
			= wlr_output_configuration_v1_create();
	Client *c;
	struct wlr_output_configuration_head_v1 *config_head;
	Monitor *m;

	/* First remove from the layout the disabled monitors */
	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output->enabled)
			continue;
		config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);
		config_head->state.enabled = 0;
		if (m->asleep)
			continue;

		/* Remove this output from the layout to avoid cursor enter inside it */
		wlr_output_layout_remove(output_layout, m->wlr_output);
		closemon(m);
		m->m = m->w = (struct wlr_box){0};
	}
	/* Insert outputs that need to */
	wl_list_for_each(m, &mons, link) {
		if (m->wlr_output->enabled
				&& !wlr_output_layout_get(output_layout, m->wlr_output))
			wlr_output_layout_add_auto(output_layout, m->wlr_output);
	}

	/* Now that we update the output layout we can get its box */
	wlr_output_layout_get_box(output_layout, NULL, &sgeom);

	wlr_scene_node_set_position(&root_bg->node, sgeom.x, sgeom.y);
	wlr_scene_rect_set_size(root_bg, sgeom.width, sgeom.height);

	/* Make sure the clients are hidden when dwl is locked */
	wlr_scene_node_set_position(&locked_bg->node, sgeom.x, sgeom.y);
	wlr_scene_rect_set_size(locked_bg, sgeom.width, sgeom.height);

	wl_list_for_each(m, &mons, link) {
		if (!m->wlr_output->enabled)
			continue;
		config_head = wlr_output_configuration_head_v1_create(config, m->wlr_output);

		/* Get the effective monitor geometry to use for surfaces */
		wlr_output_layout_get_box(output_layout, m->wlr_output, &m->m);
		m->w = m->m;
		wlr_scene_output_set_position(m->scene_output, m->m.x, m->m.y);

		wlr_scene_node_set_position(&m->fullscreen_bg->node, m->m.x, m->m.y);
		wlr_scene_rect_set_size(m->fullscreen_bg, m->m.width, m->m.height);

		wlr_scene_node_set_position(&m->blur->node, m->m.x, m->m.y);
		wlr_scene_optimized_blur_set_size(m->blur, m->m.width, m->m.height);

		if (m->lock_surface) {
			struct wlr_scene_tree *scene_tree = m->lock_surface->surface->data;
			wlr_scene_node_set_position(&scene_tree->node, m->m.x, m->m.y);
			wlr_session_lock_surface_v1_configure(m->lock_surface, m->m.width, m->m.height);
		}

		/* Calculate the effective monitor geometry to use for clients */
		arrangelayers(m);
		/* Don't move clients to the left output when plugging monitors */
		arrange(m);
		/* make sure fullscreen clients have the right size */
		if ((c = focustop(m)) && c->isfullscreen)
			resize(c, m->m, 0);

		/* Try to re-set the gamma LUT when updating monitors,
		 * it's only really needed when enabling a disabled output, but meh. */
		m->gamma_lut_changed = 1;

		config_head->state.x = m->m.x;
		config_head->state.y = m->m.y;

		if (!selmon) {
			selmon = m;
		}
	}

	/* Hand the clients of mirroring monitors to the mirrored one */
	wl_list_for_each(m, &mons, link) {
		Monitor *o = mirrorof(m);
		if (!o)
			continue;
		wl_list_for_each(c, &clients, link)
			if (c->mon == m)
				setmon(c, o, c->tags);
		if (selmon == m)
			selmon = o;
	}

	if (selmon && selmon->wlr_output->enabled) {
		wl_list_for_each(c, &clients, link) {
			if (!c->mon && client_surface(c)->mapped)
				setmon(c, selmon, c->tags);
		}
		focusclient(focustop(selmon), 1);
		if (selmon->lock_surface) {
			client_notify_enter(selmon->lock_surface->surface,
					wlr_seat_get_keyboard(seat));
			client_activate_surface(selmon->lock_surface->surface, 1);
		}
	}

	if (stext[0] == '\0')
		strncpy(stext, "", sizeof(stext));
	wl_list_for_each(m, &mons, link) {
		updatebar(m);
		drawbar(m);
		drawwallpaper(m);
	}
	dockchanged();

	/* FIXME: figure out why the cursor image is at 0,0 after turning all
	 * the monitors on.
	 * Move the cursor image where it used to be. It does not generate a
	 * wl_pointer.motion event for the clients, it's only the image what it's
	 * at the wrong position after all. */
	wlr_cursor_move(cursor, NULL, 0, 0);

	wlr_output_manager_v1_set_configuration(output_mgr, config);
}

void
updatebar(Monitor *m)
{
	size_t i;
	int rw, rh;
	char fontattrs[16];

	wlr_output_transformed_resolution(m->wlr_output, &rw, &rh);
	m->b.width = rw;
	m->b.real_width = (int)((float)m->b.width / m->wlr_output->scale);

	wlr_scene_node_set_enabled(&m->scene_buffer->node,
		m->wlr_output->enabled && !mirrorof(m) ? showbar : 0);

	for (i = 0; i < LENGTH(m->pool); i++)
		if (m->pool[i]) {
			wlr_buffer_drop(&m->pool[i]->base);
			m->pool[i] = NULL;
		}

	if (m->b.scale == m->wlr_output->scale && m->drw)
		return;

	drwl_font_destroy(m->drw->font);
	if (m->boldfnt)
		drwl_font_destroy(m->boldfnt);
	if (m->logofnt)
		drwl_font_destroy(m->logofnt);
	snprintf(fontattrs, sizeof(fontattrs), "dpi=%.2f", 96. * m->wlr_output->scale);
	if (!(m->boldfnt = drwl_font_create(NULL, LENGTH(boldfonts), boldfonts, fontattrs))
			|| !(m->logofnt = drwl_font_create(NULL, LENGTH(logofonts), logofonts, fontattrs))
			|| !(drwl_font_create(m->drw, LENGTH(fonts), fonts, fontattrs)))
		die("Could not load font");

	m->b.scale = m->wlr_output->scale;
	m->lrpad = m->drw->font->height;
	m->b.height = MAX(m->drw->font->height + 2, (int)(barheight * m->wlr_output->scale));
	m->b.real_height = (int)((float)m->b.height / m->wlr_output->scale);
}

/* Show, change or hide the tooltip for the status segment under the cursor */
void
updatetooltip(void)
{
	Monitor *m = segmon;
	int i, seg = -1, by;
	double cx;

	if (m && !locked && m->scene_buffer->node.enabled) {
		by = topbar ? m->m.y : m->m.y + m->m.height - m->b.real_height;
		if (cursor->x >= m->m.x && cursor->x < m->m.x + m->m.width
				&& cursor->y >= by && cursor->y < by + m->b.real_height) {
			cx = (cursor->x - m->m.x) * m->wlr_output->scale;
			for (i = 0; i < nseg; i++)
				if (cx >= segx[i] && cx < segx[i + 1] && tips[i][0])
					seg = i;
		}
	}
	if (seg != tipseg) {
		tipseg = seg;
		drawtooltip();
	}
}

void
updatetitle(struct wl_listener *listener, void *data)
{
	Client *c = wl_container_of(listener, c, set_title);

	if (c->foreign_toplevel_handle) {
		struct wlr_ext_foreign_toplevel_handle_v1_state foreign_toplevel_state = {
			.app_id = client_get_appid(c),
			.title = client_get_title(c),
		};
		wlr_ext_foreign_toplevel_handle_v1_update_state(c->foreign_toplevel_handle,
				&foreign_toplevel_state);
	}
	if (c->scene && c->tbar && c->th)
		drawtitle(c);
	if (c == focused_client)
		drawbars(); /* the menu bar shows the focused app */
}

void
urgent(struct wl_listener *listener, void *data)
{
	struct wlr_xdg_activation_v1_request_activate_event *event = data;
	Client *c = NULL;
	toplevel_from_wlr_surface(event->surface, &c, NULL);
	if (!c || c == focustop(selmon))
		return;

	c->isurgent = 1;
	drawbars();

	if (client_surface(c)->mapped)
		client_set_border_color(c, (float[])PCOLOR(colors[SchemeUrg][ColBorder]));
}

void
view(const Arg *arg)
{
	if (!selmon || (arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
	selmon->seltags ^= 1; /* toggle sel tagset */
	if (arg->ui & TAGMASK)
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
	focusclient(focustop(selmon), 1);
	arrange(selmon);
	drawbars();
}

void
virtualkeyboard(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_keyboard_v1 *kb = data;
	/* virtual keyboards shouldn't share keyboard group */
	KeyboardGroup *group = createkeyboardgroup();
	group->virtual_keyboard = &kb->keyboard;
	/* Set the keymap to match the group keymap */
	wlr_keyboard_set_keymap(&kb->keyboard, group->wlr_group->keyboard.keymap);
	LISTEN(&kb->keyboard.base.events.destroy, &group->destroy, destroykeyboardgroup);

	/* Add the new keyboard to the group */
	wlr_keyboard_group_add_keyboard(group->wlr_group, &kb->keyboard);
	wlr_seat_set_capabilities(seat, seat->capabilities | WL_SEAT_CAPABILITY_KEYBOARD);
}

void
virtualpointer(struct wl_listener *listener, void *data)
{
	struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
	struct wlr_input_device *device = &event->new_pointer->pointer.base;

	wlr_cursor_attach_input_device(cursor, device);
	if (event->suggested_output)
		wlr_cursor_map_input_to_output(cursor, device, event->suggested_output);
	wlr_seat_set_capabilities(seat, seat->capabilities | WL_SEAT_CAPABILITY_POINTER);
}

Monitor *
xytomon(double x, double y)
{
	struct wlr_output *o = wlr_output_layout_output_at(output_layout, x, y);
	return o ? o->data : NULL;
}

void
xytonode(double x, double y, struct wlr_surface **psurface,
		Client **pc, LayerSurface **pl, double *nx, double *ny)
{
	struct wlr_scene_node *node;
	struct wlr_surface *surface = NULL;
	struct wlr_scene_surface *scene_surface;
	int layer;

	for (layer = NUM_LAYERS - 1; !surface && layer >= 0; layer--) {
		if (layer == LyrIMPopup ||
				!(node = wlr_scene_node_at(&layers[layer]->node, x, y, nx, ny)))
			continue;

		if (node->type == WLR_SCENE_NODE_BUFFER) {
			/* skip buffers without a surface, e.g. the bar */
			if (!(scene_surface = wlr_scene_surface_try_from_buffer(
					wlr_scene_buffer_from_node(node))))
				continue;
			surface = scene_surface->surface;
		}
	}

	if (psurface) *psurface = surface;
	toplevel_from_wlr_surface(surface, pc, pl);
}

void
zoom(const Arg *arg)
{
	Client *c, *sel = focustop(selmon);

	if (!sel || !selmon || !selmon->lt[selmon->sellt]->arrange || sel->isfloating)
		return;

	/* Search for the first tiled window that is not sel, marking sel as
	 * NULL if we pass it along the way */
	wl_list_for_each(c, &clients, link) {
		if (VISIBLEON(c, selmon) && !c->isfloating) {
			if (c != sel)
				break;
			sel = NULL;
		}
	}

	/* Return if no other tiled window was found */
	if (&c->link == &clients)
		return;

	/* If we passed sel, move c to the front; otherwise, move sel to the
	 * front */
	if (!sel)
		sel = c;
	wl_list_remove(&sel->link);
	wl_list_insert(&clients, &sel->link);

	focusclient(sel, 1);
	arrange(selmon);
}


int
main(int argc, char *argv[])
{
	char *startup_cmd = NULL;
	int c;

	while ((c = getopt(argc, argv, "s:hdv")) != -1) {
		if (c == 's')
			startup_cmd = optarg;
		else if (c == 'd')
			log_level = WLR_DEBUG;
		else if (c == 'v')
			die("dwl " VERSION);
		else
			goto usage;
	}
	if (optind < argc)
		goto usage;

	/* Wayland requires XDG_RUNTIME_DIR for creating its communications socket */
	if (!getenv("XDG_RUNTIME_DIR"))
		die("XDG_RUNTIME_DIR must be set");
	setup();
	run(startup_cmd);
	cleanup();
	return EXIT_SUCCESS;

usage:
	die("Usage: %s [-v] [-d] [-s startup command]", argv[0]);
}
