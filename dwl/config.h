/* dwl 0.9 config for jorink's ThinkPad X270. Needs the patches in patches/.
 * After editing, run: rebuild-dwl (then log out and in again). */

/* Taken from https://github.com/djpohly/dwl/issues/466 */
#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }
/* appearance */
static const int sloppyfocus               = 1;  /* focus follows mouse */
static const int bypass_surface_visibility = 0;  /* 1 means idle inhibitors will disable idle tracking even if it's surface isn't visible  */
static const unsigned int borderpx         = 1;  /* border pixel of windows */
static const int resizestep                = 50; /* MOD+CTRL+hjkl resize step in pixels */
static const unsigned int snap             = 32; /* snap pixel */
static const int showbar                   = 1;  /* 0 means no bar */
static const int topbar                    = 1;  /* 0 means bottom bar */
static const char *fonts[]                 = {"JetBrainsMonoNL Nerd Font Mono:size=10"};
static const float rootcolor[]             = COLOR(0x0f0f0fff);
/* This conforms to the xdg-protocol. Set the alpha to zero to restore the old behavior */
static const float fullscreen_bg[]         = {0.0f, 0.0f, 0.0f, 1.0f}; /* You can also use glsl colors */
/* caelus by dacctal (git.symlinx.net/daccfiles, .themes/caelus), with the
 * roles from dacctal's sway config: orange accent, near-black background */
static uint32_t colors[][3]                = {
	/*               fg          bg          border    */
	[SchemeNorm] = { 0xf4decdff, 0x141514ff, 0x1e1f1eff },
	[SchemeSel]  = { 0x141514ff, 0xef934dff, 0xef934dff },
	[SchemeUrg]  = { 0x141514ff, 0xf16e65ff, 0xf16e65ff },
};

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

static const int repeat_rate = 25;
static const int repeat_delay = 600;

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
	{ MODKEY,         XKB_KEY_q,                     killclient,       {0} },
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
	/* monitors (HDMI-A-1 in extend mode) */
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

static const Button buttons[] = {
	/* left drag: tiled = swap with the window it is dropped on, floating = move
	 * right drag: tiled = resize the fair grid, floating = resize */
	{ ClkClient,   MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove} },
	{ ClkClient,   MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize} },
	{ ClkTagBar,   0,      BTN_LEFT,   view,           {0} },
};

/* no scroll bindings; dwl needs at least one entry */
static const Axis axes[] = {
	{ 0, 0, NULL, {0} },
};
