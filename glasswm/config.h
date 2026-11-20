/* glasswm config for jorink's ThinkPad X270.
 * After editing, run: up (then log out and in again). */

/* Taken from https://github.com/djpohly/dwl/issues/466 */
#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }
/* the same, premultiplied by its alpha (scene rects want that) */
#define PCOLOR(hex)   { ((hex >> 24) & 0xFF) * (hex & 0xFF) / 65025.0f, \
                        ((hex >> 16) & 0xFF) * (hex & 0xFF) / 65025.0f, \
                        ((hex >> 8) & 0xFF) * (hex & 0xFF) / 65025.0f, \
                        (hex & 0xFF) / 255.0f }
/* appearance */
static const int sloppyfocus               = 1;  /* focus follows mouse */
static const int bypass_surface_visibility = 0;  /* 1 means idle inhibitors will disable idle tracking even if it's surface isn't visible  */
static const unsigned int borderpx         = 1;  /* border pixel of windows (macOS hairline) */
static const unsigned int gappx            = 12; /* gap between tiled windows and around them */
static const int resizestep                = 50; /* MOD+CTRL+hjkl resize step in pixels */
static const unsigned int snap             = 32; /* snap pixel */
static const int showbar                   = 1;  /* 0 means no menu bar */
static const int barheight                 = 24; /* macOS menu bar height */
static const int topbar                    = 1;  /* 0 means bottom bar */
/* Inter stands in for SF Pro (13 px at size=10); the Nerd Font has the
 * status icons and the NixOS logo */
static const char *fonts[]                 = {"Inter Variable:size=10", "JetBrainsMonoNL Nerd Font Mono:size=10"};
static const char *boldfonts[]             = {"Inter Variable:size=10:weight=bold", "JetBrainsMonoNL Nerd Font Mono:size=10"};
static const char *titlefonts[]            = {"Inter Variable:size=10:weight=bold", "JetBrainsMonoNL Nerd Font Mono:size=10"};
static const char *logofonts[]             = {"JetBrainsMonoNL Nerd Font Mono:size=21"};
static const float rootcolor[]             = COLOR(0x1e1e1eff);
/* This conforms to the xdg-protocol. Set the alpha to zero to restore the old behavior */
static const float fullscreen_bg[]         = {0.0f, 0.0f, 0.0f, 1.0f}; /* You can also use glsl colors */
/* wallpaper (PNG or JPEG), scaled to cover each monitor. NULL draws a
 * macOS-like gradient. A background layer surface (swaybg) covers it. */
static const char *wallpaper               = NULL;

/* menu bar (macOS Tahoe: transparent over the wallpaper, white text with a
 * soft shadow). The logo opens logomenucmd, like the Apple menu. */
static const char *menubar_logo            = "\uf313"; /* Nerd Font: NixOS snowflake (Tux: "\uf31a") */
static const int menubar_glass             = 0;  /* 1: glass behind the menu bar */
static const int menubar_padding           = 16; /* left and right of its contents */
static const int menubar_spacing           = 20; /* between logo, app name and tags */
static const uint32_t menubar_text         = 0xffffffff;
static const uint32_t menubar_dim          = 0xffffff8c; /* tags that aren't shown */
static const uint32_t menubar_urgent       = 0xff453aff;
static const uint32_t menubar_shadow       = 0x00000059;

/* title bars, drawn for windows that ask for server-side decorations
 * (foot, LibreWolf, Qt apps, ...); GTK apps draw their own */
static const int titlebar_height           = 28;
/* glass tint of the title bar: [0] unfocused, [1] focused, premultiplied */
static const float titlebar_color[2][4]    = { PCOLOR(0x232325e0), PCOLOR(0x2c2c2ee6) };
static const float titlebar_separator[]    = PCOLOR(0x00000080);
static const uint32_t titlebar_text[2]     = { 0xffffff73, 0xffffffd9 }; /* unfocused, focused */

/* rounded corners, shadows and blur, see scenefx */
static const int corner_radius             = 16; /* macOS Tahoe window corners, 0 disables them */
static const int corner_radius_only_floating = 0; /* 1 means only floating windows have rounded corners */
/* macOS shadows: the focused window's is large and dark, the others' lighter */
static const int shadow_sigma              = 44; /* 0 disables shadows */
static const int shadow_sigma_unfocused    = 24;
static const int shadow_dy                 = 12; /* downwards offset */
static const float shadow_color[]          = COLOR(0x0000008c);
static const float shadow_alpha_unfocused  = 0.4f;
/* animations in ms, 0 disables one. Moves use a spring like macOS'. */
static const int animation_ms              = 260; /* windows moving to new places in the layout */
static const int open_ms                   = 240; /* new windows zoom in and fade in */
static const int open_wait_ms              = 200; /* most a new window waits to have drawn at its size */
static const int close_ms                  = 160; /* closed windows shrink and fade out */
static const int genie_ms                  = 520; /* minimize into the dock and back */
static const int slide_ms                  = 420; /* switching tags slides like spaces */
/* blur behind translucent windows, the dock and popups. Set num_passes to 0 to disable it */
static const struct blur_data blur_data    = { .num_passes = 3, .radius = 6, .noise = 0.02f,
                                               .brightness = 0.95f, .contrast = 0.95f, .saturation = 1.25f };
/* liquid glass drawn by the blur, in pixels. refraction is capped at the blur size */
static const struct glass_data glass_data  = { .thickness = 16, .refraction = 24,
                                               .dispersion = 0.08f, .rim = 0.35f };
/* tooltips; window borders (macOS hairline, brighter when focused) */
static uint32_t colors[][3]                = {
	/*               fg          bg          border    */
	[SchemeNorm] = { 0xffffffff, 0x1e1e1e66, 0xffffff1f }, /* bg alpha shows the glass */
	[SchemeSel]  = { 0xffffffff, 0xffffff33, 0xffffff33 },
	[SchemeUrg]  = { 0xffffffff, 0xff453aff, 0xff453aff },
};

/* dock */
static const int showdock                  = 1;
static const int dock_icon                 = 48; /* icon size */
static const int dock_pad                  = 8;  /* around the icons */
static const int dock_gap                  = 6;  /* between icons */
static const int dock_margin               = 6;  /* from the bottom of the screen */
static const int dock_radius               = 22;
static const float dock_color[]            = PCOLOR(0x5c5c6033); /* tint of its glass */
static const uint32_t dock_dot             = 0xd9ffffff; /* running app: ARGB */
static const uint32_t dock_separator       = 0x4dffffff;
static const char *icon_theme              = NULL; /* e.g. "Papirus-Dark"; hicolor is always used */

/* tagging */
static char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

/* logging */
static int log_level = WLR_ERROR;

/* dwl needs at least one rule */
static const Rule rules[] = {
	/* app_id             title                 tags mask  isfloating  monitor */
	{ NULL,               "Picture-in-Picture", 0,         1,          -1 }, /* LibreWolf video pop-out */
};

/* layout(s): only mango's fair layout */
static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "F",        fair },
};

/* monitors */
/* (x=-1, y=-1) is reserved as an "autoconfigure" monitor position indicator */
static const MonitorRule monrules[] = {
	/* name       mfact  nmaster scale layout       rotate/reflect                x    y */
	{ NULL,       0.55f, 1,      1,    &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL,   -1,  -1 },
	/* default monitor rule: can be changed but cannot be eliminated; at least one monitor rule must exist */
};

/* keyboard: empty fields use the XKB_DEFAULT_* environment variables */
static const struct xkb_rule_names xkb_rules = {
	/* can specify fields: rules, model, layout, variant, options */
	.options = NULL,
};

static const int repeat_rate = 40;
static const int repeat_delay = 300;

/* Trackpad */
static const int tap_to_click = 1;
static const int tap_and_drag = 1;
static const int drag_lock = 1;
static const int natural_scrolling = 0;
static const int disable_while_typing = 1;
static const int left_handed = 0;
static const int middle_button_emulation = 0;
static const enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
static const enum libinput_config_click_method click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
static const uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
static const enum libinput_config_accel_profile accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
static const double accel_speed = 0.0;
static const enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

/* Super (Windows) key */
#define MODKEY WLR_MODIFIER_LOGO
#define SHIFT  WLR_MODIFIER_SHIFT
#define CTRL   WLR_MODIFIER_CTRL

/* MOD+n views tag n, MOD+SHIFT+n moves the focused window there and follows.
 * Keysyms are modifier-independent in dwl 0.9, so SHIFT+1 is still XKB_KEY_1. */
#define TAGKEYS(KEY,TAG) \
	{ MODKEY,       KEY, view,      {.ui = 1 << TAG} }, \
	{ MODKEY|SHIFT, KEY, tagfollow, {.ui = 1 << TAG} }

/* commands (scripts live in ~/.local/bin, see dotfiles/) */
static const char *termcmd[]     = { "footclient", NULL };
static const char *filescmd[]    = { "footclient", "nnn", NULL };
static const char *upgradecmd[]  = { "footclient", "--hold", "up", NULL };
static const char *browsercmd[]  = { "librewolf", NULL };
static const char *appmenucmd[]  = { "menu-apps", NULL };
static const char *sysmenucmd[]  = { "menu-system", NULL };
static const char *shotcmd[]     = { "menu-screenshot", "region", NULL };
static const char *lockcmd[]     = { "lock", NULL };
static const char *lockslpcmd[]  = { "lock-suspend", NULL };
static const char *sessmenucmd[] = { "menu-session", NULL };
static const char *clipmenucmd[] = { "menu-clipboard", NULL };
static const char *gitmenucmd[]  = { "menu-git", NULL };
static const char **logomenucmd  = sysmenucmd; /* the menu bar's logo */
static const char *volupcmd[]    = { "volume", "up", NULL };
static const char *voldowncmd[]  = { "volume", "down", NULL };
static const char *volmutecmd[]  = { "volume", "mute", NULL };
static const char *micmutecmd[]  = { "volume", "micmute", NULL };
static const char *brupcmd[]     = { "brightness", "up", NULL };
static const char *brdowncmd[]   = { "brightness", "down", NULL };

static const Key keys[] = {
	/* modifier       key                            function          argument */
	{ MODKEY,         XKB_KEY_Return,                spawn,            {.v = termcmd} },
	{ MODKEY,         XKB_KEY_e,                     spawn,            {.v = filescmd} },
	{ MODKEY,         XKB_KEY_u,                     spawn,            {.v = upgradecmd} },
	{ MODKEY,         XKB_KEY_b,                     spawn,            {.v = browsercmd} },
	{ MODKEY,         XKB_KEY_space,                 spawn,            {.v = appmenucmd} },
	{ MODKEY,         XKB_KEY_s,                     spawn,            {.v = sysmenucmd} },
	{ MODKEY|SHIFT,   XKB_KEY_s,                     spawn,            {.v = shotcmd} },
	{ MODKEY|SHIFT,   XKB_KEY_p,                     spawn,            {.v = lockcmd} },
	{ MODKEY|CTRL,    XKB_KEY_p,                     spawn,            {.v = lockslpcmd} },
	{ MODKEY,         XKB_KEY_p,                     spawn,            {.v = sessmenucmd} },
	{ MODKEY,         XKB_KEY_v,                     spawn,            {.v = clipmenucmd} },
	{ MODKEY,         XKB_KEY_g,                     spawn,            {.v = gitmenucmd} },
	{ MODKEY,         XKB_KEY_q,                     killclient,       {0} },
	{ MODKEY,         XKB_KEY_m,                     minimizesel,      {0} }, /* macOS: Cmd+M */
	{ MODKEY|SHIFT,   XKB_KEY_m,                     quit,             {0} },
	{ MODKEY,         XKB_KEY_backslash,             togglefloating,   {0} },
	{ MODKEY,         XKB_KEY_f,                     togglemaximize,   {0} },
	{ MODKEY|SHIFT,   XKB_KEY_f,                     togglefullscreen, {0} },
	/* focus */
	{ MODKEY,         XKB_KEY_h,                     focusdir,         {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY,         XKB_KEY_j,                     focusdir,         {.i = WLR_DIRECTION_DOWN} },
	{ MODKEY,         XKB_KEY_k,                     focusdir,         {.i = WLR_DIRECTION_UP} },
	{ MODKEY,         XKB_KEY_l,                     focusdir,         {.i = WLR_DIRECTION_RIGHT} },
	/* swap with the neighbour */
	{ MODKEY|SHIFT,   XKB_KEY_h,                     exchangeclient,   {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|SHIFT,   XKB_KEY_j,                     exchangeclient,   {.i = WLR_DIRECTION_DOWN} },
	{ MODKEY|SHIFT,   XKB_KEY_k,                     exchangeclient,   {.i = WLR_DIRECTION_UP} },
	{ MODKEY|SHIFT,   XKB_KEY_l,                     exchangeclient,   {.i = WLR_DIRECTION_RIGHT} },
	/* resize: tiled windows move the fair grid boundary, floating ones resize */
	{ MODKEY|CTRL,    XKB_KEY_h,                     resizedir,        {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|CTRL,    XKB_KEY_j,                     resizedir,        {.i = WLR_DIRECTION_DOWN} },
	{ MODKEY|CTRL,    XKB_KEY_k,                     resizedir,        {.i = WLR_DIRECTION_UP} },
	{ MODKEY|CTRL,    XKB_KEY_l,                     resizedir,        {.i = WLR_DIRECTION_RIGHT} },
	/* monitors (external display in extend mode, see menu-display) */
	{ MODKEY,         XKB_KEY_comma,                 focusmon,         {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY,         XKB_KEY_period,                focusmon,         {.i = WLR_DIRECTION_RIGHT} },
	{ MODKEY|SHIFT,   XKB_KEY_comma,                 tagmon,           {.i = WLR_DIRECTION_LEFT} },
	{ MODKEY|SHIFT,   XKB_KEY_period,                tagmon,           {.i = WLR_DIRECTION_RIGHT} },
	/* media keys */
	{ 0,              XKB_KEY_XF86AudioRaiseVolume,  spawn,            {.v = volupcmd} },
	{ 0,              XKB_KEY_XF86AudioLowerVolume,  spawn,            {.v = voldowncmd} },
	{ 0,              XKB_KEY_XF86AudioMute,         spawn,            {.v = volmutecmd} },
	{ 0,              XKB_KEY_XF86AudioMicMute,      spawn,            {.v = micmutecmd} },
	{ 0,              XKB_KEY_XF86MonBrightnessUp,   spawn,            {.v = brupcmd} },
	{ 0,              XKB_KEY_XF86MonBrightnessDown, spawn,            {.v = brdowncmd} },
	TAGKEYS(          XKB_KEY_1,                     0),
	TAGKEYS(          XKB_KEY_2,                     1),
	TAGKEYS(          XKB_KEY_3,                     2),
	TAGKEYS(          XKB_KEY_4,                     3),
	TAGKEYS(          XKB_KEY_5,                     4),
	TAGKEYS(          XKB_KEY_6,                     5),
	TAGKEYS(          XKB_KEY_7,                     6),
	TAGKEYS(          XKB_KEY_8,                     7),
	TAGKEYS(          XKB_KEY_9,                     8),

	/* Ctrl-Alt-Fn switches VTs: the way out if the session hangs */
#define CHVT(n) { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT,XKB_KEY_F##n, chvt, {.ui = (n)} }
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
};

/* dock: pinned apps, left to right. Running apps that aren't here are
 * added after them while they run. */
static const DockApp dockapps[] = {
	/* app_id          command */
	{ "foot",          termcmd },
	{ "librewolf",     browsercmd },
};

static const Button buttons[] = {
	/* left drag: tiled = swap with the window it is dropped on, floating = move
	 * right drag: tiled = resize the fair grid, floating = resize */
	{ ClkClient,   MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove} },
	{ ClkClient,   MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize} },
};

/* no scroll bindings; dwl needs at least one entry */
static const Axis axes[] = {
	{ 0, 0, NULL, {0} },
};
