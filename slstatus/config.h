/* slstatus 1.1 config: status text for dwl's bar (slstatus -s | dwl).
 * Needs the patches in patches/. After editing, run: rebuild-slstatus */

/* interval between updates (in ms); SIGUSR1 updates immediately */
const unsigned int interval = 2000;

/* text to show if no value can be retrieved */
static const char unknown_str[] = "n/a";

/* dwl's bar keeps at most 511 bytes of status text */
#define MAXLEN 512

/*
 * Segments and tooltips (dwl bar-tooltips patch): "\x1f" starts a new bar
 * segment, "\x1e" starts the tooltip texts, one per segment in the same
 * order, again separated by "\x1f". Keep "\x1f" in its own string literal:
 * "\x1fBAT" would be read as the escape \x1fBA.
 *
 * Nerd Font icons: U+F4BC cpu, U+F035B memory, U+F0954 clock.
 * "brightness", "volume" and their "-info" files are read from the working
 * directory, $XDG_RUNTIME_DIR (see ~/.local/bin/dwl-session);
 * ~/.local/bin/brightness and ~/.local/bin/volume write them and send
 * SIGUSR1. Reading a file costs no fork, unlike running brightnessctl or
 * wpctl every 2 seconds.
 * battery_state prints a charging/discharging/idle/full icon
 * (battery-icons patch), followed by that battery's percentage.
 */
static const struct arg args[] = {
	/* function       format                          argument */
	/* bar */
	{ cpu_perc,       " %s%%  ",                NULL },
	{ ram_perc,       "\x1f" "\U000f035b %s%%  ",     NULL },
	{ cat,            "\x1f" "%s  ",                  "brightness" },
	{ cat,            "\x1f" "%s  ",                  "volume" },
	{ battery_state,  "\x1f" "%s ",                   "BAT0" },
	{ battery_perc,   "%s%%  ",                       "BAT0" },
	{ battery_state,  "\x1f" "%s ",                   "BAT1" },
	{ battery_perc,   "%s%%  ",                       "BAT1" },
	{ datetime,       "\x1f" "\U000f0954 %s ",        "%H:%M" },
	/* tooltips, in the same order */
	{ cpu_freq,       "\x1e" "CPU %sHz",              NULL },
	{ load_avg,       ", load %s",                    NULL },
	{ ram_used,       "\x1f" "RAM %sB used",          NULL },
	{ ram_total,      " of %sB",                      NULL },
	{ swap_used,      ", swap %sB",                   NULL },
	{ swap_total,     " of %sB",                      NULL },
	{ cat,            "\x1f" "%s",                    "brightness-info" },
	{ cat,            "\x1f" "%s",                    "volume-info" },
	{ battery_info,   "\x1f" "BAT0: %s",              "BAT0" },
	{ battery_info,   "\x1f" "BAT1: %s",              "BAT1" },
	{ datetime,       "\x1f" "%s",                    "%A %d %B %Y" },
};
