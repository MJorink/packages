/* icons.h: app names and icons for the dock and the menu bar, from the
 * desktop entries and icon themes in the XDG data dirs. Looked up once per
 * app_id and cached. */
#include <ctype.h>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

uint32_t *img_load(const char *path, int svgsize, int *w, int *h); /* images.c */

typedef struct App {
	char id[128];   /* app_id of its windows */
	char name[128]; /* Name= of its desktop entry, else derived from the id */
	char icon[PATH_MAX]; /* icon file, "" if none was found */
	uint32_t *px;   /* the icon rendered at size x size px, premultiplied ARGB */
	int size;
	struct App *next;
} App;

static App *apps;
static char datadir[16][PATH_MAX];
static int ndatadirs;

static void
adddatadir(const char *d)
{
	int i;
	struct stat st;
	if (!d || !*d || ndatadirs >= (int)LENGTH(datadir) || stat(d, &st) || !S_ISDIR(st.st_mode))
		return;
	for (i = 0; i < ndatadirs; i++)
		if (!strcmp(datadir[i], d))
			return;
	snprintf(datadir[ndatadirs++], PATH_MAX, "%s", d);
}

/* $XDG_DATA_HOME, $XDG_DATA_DIRS and NixOS' profiles, in that order */
static void
initdatadirs(void)
{
	char buf[PATH_MAX], *dirs, *d, *save = NULL;
	const char *home = getenv("HOME"), *user = getenv("USER");

	if (ndatadirs)
		return;
	if (getenv("XDG_DATA_HOME"))
		adddatadir(getenv("XDG_DATA_HOME"));
	else if (home && snprintf(buf, sizeof(buf), "%s/.local/share", home) > 0)
		adddatadir(buf);
	dirs = strdup(getenv("XDG_DATA_DIRS") ? getenv("XDG_DATA_DIRS") : "/usr/local/share:/usr/share");
	for (d = strtok_r(dirs, ":", &save); d; d = strtok_r(NULL, ":", &save))
		adddatadir(d);
	free(dirs);
	if (user && snprintf(buf, sizeof(buf), "/etc/profiles/per-user/%s/share", user) > 0)
		adddatadir(buf);
	if (home && snprintf(buf, sizeof(buf), "%s/.nix-profile/share", home) > 0)
		adddatadir(buf);
	adddatadir("/run/current-system/sw/share");
}

/* Read Name=, Icon= and StartupWMClass= of a desktop entry's main group */
static int
readdesktop(const char *path, char *name, char *icon, char *wmclass)
{
	char line[1024];
	int main = 0, nodisplay = 0;
	FILE *f = fopen(path, "r");

	if (!f)
		return 0;
	name[0] = icon[0] = wmclass[0] = '\0';
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = '\0';
		if (line[0] == '[') {
			if (main)
				break; /* past [Desktop Entry] */
			main = !strcmp(line, "[Desktop Entry]");
		} else if (main && !strncmp(line, "Name=", 5)) {
			snprintf(name, 128, "%s", line + 5);
		} else if (main && !strncmp(line, "Icon=", 5)) {
			snprintf(icon, PATH_MAX, "%s", line + 5);
		} else if (main && !strncmp(line, "StartupWMClass=", 15)) {
			snprintf(wmclass, 128, "%s", line + 15);
		} else if (main && !strcmp(line, "NoDisplay=true")) {
			nodisplay = 1;
		}
	}
	fclose(f);
	return nodisplay ? 1 : 2;
}

/* The desktop entry of app_id id: <id>.desktop, else one whose
 * StartupWMClass or last dotted name part (org.gnome.Nautilus) is id */
static int
finddesktop(const char *id, char *name, char *icon)
{
	char path[PATH_MAX], wmclass[128], lower[128];
	int i, j, fallback = 0;
	DIR *dir;
	struct dirent *e;

	for (j = 0; id[j] && j < (int)sizeof(lower) - 1; j++)
		lower[j] = (char)tolower((unsigned char)id[j]);
	lower[j] = '\0';
	for (i = 0; i < ndatadirs; i++) {
		snprintf(path, sizeof(path), "%s/applications/%s.desktop", datadir[i], id);
		if (readdesktop(path, name, icon, wmclass))
			return 1;
		snprintf(path, sizeof(path), "%s/applications/%s.desktop", datadir[i], lower);
		if (readdesktop(path, name, icon, wmclass))
			return 1;
	}
	for (i = 0; i < ndatadirs; i++) {
		snprintf(path, sizeof(path), "%s/applications", datadir[i]);
		if (!(dir = opendir(path)))
			continue;
		while ((e = readdir(dir))) {
			char *dot, base[256];
			size_t n = strlen(e->d_name);
			if (n < 9 || strcmp(e->d_name + n - 8, ".desktop"))
				continue;
			snprintf(base, sizeof(base), "%.*s", (int)(n - 8), e->d_name);
			dot = strrchr(base, '.');
			snprintf(path, sizeof(path), "%s/applications/%s", datadir[i], e->d_name);
			if (!fallback && dot && !strcasecmp(dot + 1, id)
					&& readdesktop(path, name, icon, wmclass) == 2) {
				fallback = 1; /* keep looking for a StartupWMClass match */
				continue;
			}
			if (readdesktop(path, name, icon, wmclass) && !strcasecmp(wmclass, id)) {
				closedir(dir);
				return 1;
			}
		}
		closedir(dir);
	}
	if (fallback) {
		/* read the fallback again: the scan overwrote name and icon */
		for (i = 0; i < ndatadirs; i++) {
			snprintf(path, sizeof(path), "%s/applications", datadir[i]);
			if (!(dir = opendir(path)))
				continue;
			while ((e = readdir(dir))) {
				size_t n = strlen(e->d_name);
				char *dot;
				if (n < 9 || strcmp(e->d_name + n - 8, ".desktop"))
					continue;
				e->d_name[n - 8] = '\0';
				dot = strrchr(e->d_name, '.');
				if (dot && !strcasecmp(dot + 1, id)) {
					snprintf(path, sizeof(path), "%s/applications/%s.desktop",
							datadir[i], e->d_name);
					closedir(dir);
					return readdesktop(path, name, icon, wmclass) == 2;
				}
			}
			closedir(dir);
		}
	}
	return 0;
}

static int
exists(const char *path)
{
	return !access(path, R_OK);
}

/* The Inherits= themes of an icon theme */
static void
inherits(const char *theme, char *out, size_t n)
{
	char path[PATH_MAX], line[512];
	int i;
	FILE *f;
	out[0] = '\0';
	for (i = 0; i < ndatadirs; i++) {
		snprintf(path, sizeof(path), "%s/icons/%s/index.theme", datadir[i], theme);
		if (!(f = fopen(path, "r")))
			continue;
		while (fgets(line, sizeof(line), f))
			if (!strncmp(line, "Inherits=", 9)) {
				line[strcspn(line, "\r\n")] = '\0';
				snprintf(out, n, "%s", line + 9);
				break;
			}
		fclose(f);
		return;
	}
}

/* Look for icon name in theme (and the themes it inherits, depth levels) */
static int
findthemeicon(const char *theme, const char *name, int depth, char *out)
{
	static const char *subdirs[] = { "scalable/apps", "512x512/apps", "256x256/apps",
		"128x128/apps", "96x96/apps", "64x64/apps", "48x48/apps", "32x32/apps",
		"scalable", "256x256", "128x128", "64x64", "48x48", /* flat themes */
		"apps/scalable", "apps/64", "apps/48", "apps/32" };
	static const char *ext[] = { "svg", "png" };
	char inh[256], *t, *save = NULL;
	size_t i, j;
	int d;

	for (d = 0; d < ndatadirs; d++)
		for (i = 0; i < LENGTH(subdirs); i++)
			for (j = 0; j < LENGTH(ext); j++) {
				snprintf(out, PATH_MAX, "%s/icons/%s/%s/%s.%s",
						datadir[d], theme, subdirs[i], name, ext[j]);
				if (exists(out))
					return 1;
			}
	if (depth <= 0)
		return 0;
	inherits(theme, inh, sizeof(inh));
	for (t = strtok_r(inh, ",", &save); t; t = strtok_r(NULL, ",", &save))
		if (strcmp(t, "hicolor") && findthemeicon(t, name, depth - 1, out))
			return 1;
	return 0;
}

static int
findicon(const char *name, char *out)
{
	static const char *ext[] = { "svg", "png" };
	size_t j;
	int d;

	if (!name[0])
		return 0;
	if (name[0] == '/') {
		snprintf(out, PATH_MAX, "%s", name);
		return exists(out);
	}
	if ((icon_theme && findthemeicon(icon_theme, name, 3, out))
			|| findthemeicon("hicolor", name, 0, out))
		return 1;
	for (d = 0; d < ndatadirs; d++)
		for (j = 0; j < LENGTH(ext); j++) {
			snprintf(out, PATH_MAX, "%s/pixmaps/%s.%s", datadir[d], name, ext[j]);
			if (exists(out))
				return 1;
		}
	return 0;
}

/* src (sw x sh) scaled to fit size x size, centred, with a good filter */
static uint32_t *
fitimage(uint32_t *src, int sw, int sh, int size)
{
	uint32_t *dst = ecalloc((size_t)size * size, 4);
	pixman_image_t *s = pixman_image_create_bits(PIXMAN_a8r8g8b8, sw, sh, src, sw * 4);
	pixman_image_t *d = pixman_image_create_bits(PIXMAN_a8r8g8b8, size, size, dst, size * 4);
	double f = (double)MAX(sw, sh) / size;
	int w = (int)(sw / f + 0.5), h = (int)(sh / f + 0.5), nparams;
	pixman_transform_t t;
	pixman_fixed_t *params;

	pixman_transform_init_scale(&t, pixman_double_to_fixed(f), pixman_double_to_fixed(f));
	pixman_image_set_transform(s, &t);
	params = pixman_filter_create_separable_convolution(&nparams,
			pixman_double_to_fixed(f), pixman_double_to_fixed(f),
			PIXMAN_KERNEL_IMPULSE, PIXMAN_KERNEL_IMPULSE,
			PIXMAN_KERNEL_LANCZOS2, PIXMAN_KERNEL_LANCZOS2,
			f > 1 ? 2 : 1, f > 1 ? 2 : 1);
	pixman_image_set_filter(s, PIXMAN_FILTER_SEPARABLE_CONVOLUTION, params, nparams);
	free(params);
	pixman_image_composite32(PIXMAN_OP_SRC, s, NULL, d, 0, 0, 0, 0,
			(size - w) / 2, (size - h) / 2, w, h);
	pixman_image_unref(s);
	pixman_image_unref(d);
	return dst;
}

/* A generic icon: a grey rounded square with the name's initial */
static uint32_t *
genericicon(const char *name, int size)
{
	uint32_t *px = ecalloc((size_t)size * size, 4);
	float s = (float)size, m = s * 0.08f;
	uint32_t scheme[3] = { 0xffffffff, 0 };
	char initial[2] = { (char)toupper((unsigned char)name[0]), 0 };
	Drwl *d;
	char attrs[32];
	int y;

	/* a vertical gradient from light to dark grey, a row at a time */
	for (y = 0; y < size; y++) {
		uint32_t g = (uint32_t)(0x8e - 0x40 * y / size);
		float shape[5] = { m, m, s - 2 * m, s - 2 * m, s * 0.225f };
		paint(px, size, size, 0, (float)y, s, (float)y + 1, inrrect, shape,
				0xff000000 | g << 16 | g << 8 | (g + 8));
	}
	if (!(d = drwl_create()))
		return px;
	drwl_image_create(d, (unsigned)size, (unsigned)size, px);
	snprintf(attrs, sizeof(attrs), "pixelsize=%d", size / 2);
	if (drwl_font_create(d, LENGTH(boldfonts), boldfonts, attrs)) {
		int tw = (int)drwl_font_getwidth(d, initial);
		drwl_setscheme(d, scheme);
		drwl_text(d, (size - tw) / 2, 0, (unsigned)tw, (unsigned)size, 0, initial, 0);
	}
	drwl_destroy(d); /* and its font and image */
	return px;
}

/* The app of app_id id with its icon at size px */
static App *
appget(const char *id, int size)
{
	App *a;
	char iconname[PATH_MAX];
	const char *dot;

	if (!id || !*id)
		id = "unknown";
	for (a = apps; a; a = a->next)
		if (!strcmp(a->id, id))
			break;
	if (!a) {
		initdatadirs();
		a = ecalloc(1, sizeof(*a));
		snprintf(a->id, sizeof(a->id), "%s", id);
		if (!finddesktop(id, a->name, iconname) || !a->name[0]) {
			/* org.example.Name -> Name, foot -> Foot */
			dot = strrchr(id, '.');
			snprintf(a->name, sizeof(a->name), "%s", dot && dot[1] ? dot + 1 : id);
			a->name[0] = (char)toupper((unsigned char)a->name[0]);
			snprintf(iconname, sizeof(iconname), "%s", id);
		}
		if (!findicon(iconname, a->icon) && !findicon(id, a->icon))
			a->icon[0] = '\0';
		a->next = apps;
		apps = a;
	}
	if (size && a->size != size) {
		uint32_t *src = NULL;
		int w, h;
		free(a->px);
		if (a->icon[0] && (src = img_load(a->icon, size, &w, &h))) {
			a->px = fitimage(src, w, h, size);
			free(src);
		} else {
			a->px = genericicon(a->name, size);
		}
		a->size = size;
	}
	return a;
}
