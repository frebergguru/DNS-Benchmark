#include "theme.h"

#include "../util/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===================================================================
 * Color palettes mirror https://www.freberg.guru:
 *
 * Dark (CRT amber-on-black):
 *   bg=#000  fg=#FFBF00  fg_bright=#FFE066  fg_dim=#B07F00
 *
 * Light (parchment):
 *   bg=#F2E5C7  fg=#5A3A00  fg_bright=#3D2700  fg_dim=#8A6520
 *
 * Bar/status colors are tuned to harmonize with each palette while
 * keeping the three lookup classes visually distinguishable.
 * =================================================================== */

static const dnsb_theme_colors PALETTE_DARK = {
    .bg        = { 0.000, 0.000, 0.000 },              /* #000 */
    .fg        = { 1.000, 0.749, 0.000 },              /* #FFBF00 amber */
    .fg_bright = { 1.000, 0.878, 0.400 },              /* #FFE066 */
    .fg_dim    = { 0.690, 0.498, 0.000 },              /* #B07F00 */
    .fg_faint_a = 0.06,
    .bar_cached   = { 1.000, 0.749, 0.000 },           /* amber (primary) */
    .bar_uncached = { 0.420, 1.000, 0.420 },           /* CRT green */
    .bar_dotcom   = { 1.000, 0.478, 1.000 },           /* CRT magenta */
    .status_ok        = { 0.420, 1.000, 0.420 },
    .status_fail      = { 1.000, 0.380, 0.380 },
    .status_redirect  = { 1.000, 0.620, 0.180 },
    .status_sidelined = { 0.500, 0.500, 0.500 },
    .status_idle      = { 0.690, 0.498, 0.000 },
    .pin_ring         = { 1.000, 0.878, 0.400 },
    .is_dark = 1,
};

static const dnsb_theme_colors PALETTE_LIGHT = {
    .bg        = { 0.949, 0.898, 0.780 },              /* #F2E5C7 parchment */
    .fg        = { 0.353, 0.227, 0.000 },              /* #5A3A00 */
    .fg_bright = { 0.239, 0.153, 0.000 },              /* #3D2700 */
    .fg_dim    = { 0.541, 0.396, 0.125 },              /* #8A6520 */
    .fg_faint_a = 0.07,
    .bar_cached   = { 0.620, 0.220, 0.110 },           /* brick red */
    .bar_uncached = { 0.180, 0.420, 0.180 },           /* deep moss green */
    .bar_dotcom   = { 0.490, 0.220, 0.580 },           /* aubergine */
    .status_ok        = { 0.180, 0.420, 0.180 },
    .status_fail      = { 0.620, 0.180, 0.180 },
    .status_redirect  = { 0.720, 0.420, 0.080 },
    .status_sidelined = { 0.490, 0.420, 0.300 },
    .status_idle      = { 0.541, 0.396, 0.125 },
    .pin_ring         = { 0.720, 0.510, 0.080 },
    .is_dark = 0,
};

static GtkCssProvider *g_provider = NULL;
static dnsb_theme_mode g_mode = DNSB_THEME_AUTO;
static const dnsb_theme_colors *g_active = &PALETTE_DARK;

typedef struct listener {
    void (*cb)(void *user);
    void *user;
    struct listener *next;
} listener;

static listener *g_listeners = NULL;

const char *dnsb_theme_mode_label(dnsb_theme_mode m) {
    switch (m) {
        case DNSB_THEME_DARK:  return "dark";
        case DNSB_THEME_LIGHT: return "light";
        default:               return "auto";
    }
}

static char *config_path(void) {
    const char *xdg = g_getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return g_build_filename(xdg, "DNS-Benchmark", "theme", NULL);
    const char *home = g_get_home_dir();
    return g_build_filename(home, ".config", "DNS-Benchmark", "theme", NULL);
}

static dnsb_theme_mode load_pref(void) {
    char *path = config_path();
    gchar *contents = NULL;
    gsize len = 0;
    dnsb_theme_mode mode = DNSB_THEME_AUTO;
    if (g_file_get_contents(path, &contents, &len, NULL)) {
        g_strstrip(contents);
        if      (g_ascii_strcasecmp(contents, "dark")  == 0) mode = DNSB_THEME_DARK;
        else if (g_ascii_strcasecmp(contents, "light") == 0) mode = DNSB_THEME_LIGHT;
        else                                                  mode = DNSB_THEME_AUTO;
        g_free(contents);
    }
    g_free(path);
    return mode;
}

static void save_pref(dnsb_theme_mode mode) {
    char *path = config_path();
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    g_file_set_contents(path, dnsb_theme_mode_label(mode), -1, NULL);
    g_free(path);
}

static int detect_dark_preferred(void) {
    /* GTK_THEME env can force a theme; honour it if it contains the word "dark". */
    const char *e = g_getenv("GTK_THEME");
    if (e && (strstr(e, "dark") || strstr(e, "Dark"))) return 1;
    /* gsettings color-scheme on GNOME 42+. */
    e = g_getenv("DNSB_PREFER_DARK");
    if (e && *e == '1') return 1;
    /* Default: dark, matching freberg.guru's default. */
    return 1;
}

static const char *css_for(int dark) {
    /* Dark and light palettes share structure — we emit the CSS at runtime so
       the hex values stay close to the palette table above. */
    if (dark) {
        return
        "@define-color dnsb_bg        #000000;\n"
        "@define-color dnsb_fg        #FFBF00;\n"
        "@define-color dnsb_fg_bright #FFE066;\n"
        "@define-color dnsb_fg_dim    #B07F00;\n"
        "@define-color dnsb_fg_faint  alpha(#FFBF00, 0.06);\n"
        "@define-color dnsb_glow      alpha(#FFBF00, 0.35);\n"
        ".dnsb-monospace { font-family: 'Cascadia Mono','JetBrains Mono','DejaVu Sans Mono',monospace; }\n"
        "* { font-family: 'Cascadia Mono','JetBrains Mono','DejaVu Sans Mono',monospace; text-shadow: none; -gtk-icon-shadow: none; }\n"
        "window, dialog { background-color: @dnsb_bg; color: @dnsb_fg; }\n"
        "headerbar { background: @dnsb_bg; color: @dnsb_fg; border-bottom: 1px solid @dnsb_fg; padding: 4px 8px; box-shadow: none; }\n"
        "headerbar .title { color: @dnsb_fg_bright; }\n"
        "headerbar .subtitle { color: @dnsb_fg_dim; }\n"
        "label { color: @dnsb_fg; }\n"
        "button { background: transparent; color: @dnsb_fg_dim; border: 1px solid @dnsb_fg_dim; border-radius: 0; padding: 4px 12px; box-shadow: none; }\n"
        "button label, button image { color: inherit; }\n"
        "button:hover { background: @dnsb_fg; color: @dnsb_bg; border-color: @dnsb_fg; }\n"
        "button:active { background: @dnsb_fg_bright; color: @dnsb_bg; border-color: @dnsb_fg_bright; }\n"
        "button:disabled { color: alpha(@dnsb_fg_dim, 0.4); border-color: alpha(@dnsb_fg_dim, 0.3); background: transparent; }\n"
        "button.suggested-action { background: @dnsb_fg; color: @dnsb_bg; border-color: @dnsb_fg; }\n"
        "button.suggested-action:hover { background: @dnsb_fg_bright; border-color: @dnsb_fg_bright; }\n"
        /* Window-control buttons (minimize / maximize / close) in the headerbar. */
        "headerbar windowcontrols, headerbar windowcontrols box { background: transparent; }\n"
        "button.titlebutton, headerbar windowcontrols button { background: transparent; border: none; padding: 4px 6px; min-width: 22px; min-height: 22px; color: @dnsb_fg; }\n"
        "button.titlebutton image, headerbar windowcontrols button image { color: @dnsb_fg; }\n"
        "button.titlebutton:hover, headerbar windowcontrols button:hover { background: @dnsb_fg_faint; color: @dnsb_fg_bright; border: none; }\n"
        "button.titlebutton:hover image, headerbar windowcontrols button:hover image { color: @dnsb_fg_bright; }\n"
        "button.titlebutton.close:hover, headerbar windowcontrols button.close:hover { background: @dnsb_fg; color: @dnsb_bg; }\n"
        "button.titlebutton.close:hover image, headerbar windowcontrols button.close:hover image { color: @dnsb_bg; }\n"
        "notebook { background: @dnsb_bg; }\n"
        "notebook > header { background: @dnsb_bg; border-color: @dnsb_fg_dim; }\n"
        "notebook tab { background: transparent; color: @dnsb_fg_dim; border-radius: 0; padding: 6px 16px; border: 1px solid transparent; }\n"
        "notebook tab:checked { color: @dnsb_fg_bright; border-color: @dnsb_fg; background: @dnsb_fg_faint; }\n"
        "notebook tab:hover { color: @dnsb_fg; }\n"
        "entry { background: @dnsb_bg; color: @dnsb_fg; border: 1px solid @dnsb_fg_dim; border-radius: 0; padding: 4px 6px; box-shadow: none; }\n"
        "entry:focus { border-color: @dnsb_fg_bright; box-shadow: none; }\n"
        "spinbutton { background: @dnsb_bg; color: @dnsb_fg; border: 1px solid @dnsb_fg_dim; border-radius: 0; padding: 0; box-shadow: none; min-height: 0; }\n"
        "spinbutton:focus-within { border-color: @dnsb_fg_bright; }\n"
        "spinbutton entry { background: transparent; border: none; color: @dnsb_fg; min-height: 0; padding: 4px 6px; box-shadow: none; }\n"
        "spinbutton entry:focus { border: none; box-shadow: none; }\n"
        "spinbutton button { border: none; background: transparent; color: @dnsb_fg; min-width: 22px; min-height: 0; padding: 0 4px; margin: 0; box-shadow: none; }\n"
        "spinbutton button:hover { background: @dnsb_fg; color: @dnsb_bg; }\n"
        "spinbutton button:active { background: @dnsb_fg_bright; color: @dnsb_bg; }\n"
        "spinbutton button:disabled { color: alpha(@dnsb_fg_dim, 0.4); background: transparent; }\n"
        "combobox button { background: @dnsb_bg; color: @dnsb_fg; border: 1px solid @dnsb_fg_dim; padding: 4px 8px; }\n"
        "combobox arrow { color: @dnsb_fg; min-width: 12px; min-height: 12px; }\n"
        "scrollbar button { background: transparent; border: none; color: @dnsb_fg_dim; min-width: 0; min-height: 0; padding: 0; margin: 0; }\n"
        "scrollbar button:hover { background: @dnsb_fg_faint; color: @dnsb_fg; }\n"
        "progressbar > trough { background: @dnsb_bg; border: 1px solid @dnsb_fg_dim; border-radius: 0; }\n"
        "progressbar > trough > progress { background: @dnsb_fg; border-radius: 0; min-height: 4px; }\n"
        "progressbar > text { color: @dnsb_fg_bright; }\n"
        "scrolledwindow, scrolledwindow viewport { background: @dnsb_bg; }\n"
        "scrollbar { background: @dnsb_bg; border: none; }\n"
        "scrollbar slider { background: @dnsb_fg_dim; min-width: 6px; min-height: 6px; border-radius: 0; }\n"
        "scrollbar slider:hover { background: @dnsb_fg; }\n"
        "treeview, treeview.view { background: @dnsb_bg; color: @dnsb_fg; }\n"
        "treeview:selected, treeview.view:selected { background: @dnsb_fg; color: @dnsb_bg; }\n"
        "treeview header button, columnheader button { background: @dnsb_bg; color: @dnsb_fg_dim; border: none; border-bottom: 1px solid @dnsb_fg_dim; border-radius: 0; padding: 4px 8px; }\n"
        "treeview header button:hover, columnheader button:hover { color: @dnsb_fg_bright; background: @dnsb_fg_faint; }\n"
        "textview, textview text { background: @dnsb_bg; color: @dnsb_fg; }\n"
        "textview text selection { background: @dnsb_fg; color: @dnsb_bg; }\n"
        "separator { background: @dnsb_fg_dim; }\n"
        "menu, popover { background: @dnsb_bg; color: @dnsb_fg; border: 1px solid @dnsb_fg; }\n"
        "menuitem { color: @dnsb_fg; padding: 4px 8px; }\n"
        "menuitem:hover { background: @dnsb_fg; color: @dnsb_bg; }\n"
        "tooltip { background: @dnsb_bg; color: @dnsb_fg; border: 1px solid @dnsb_fg; }\n"
        "tooltip label { color: @dnsb_fg; }\n"
        "checkbutton, radiobutton { color: @dnsb_fg; }\n"
        "checkbutton check, radiobutton radio { background: @dnsb_bg; border: 1px solid @dnsb_fg_dim; min-width: 14px; min-height: 14px; box-shadow: none; }\n"
        "checkbutton check:checked, radiobutton radio:checked { background: @dnsb_fg; color: @dnsb_bg; }\n"
        ;
    }
    return
        "@define-color dnsb_bg        #F2E5C7;\n"
        "@define-color dnsb_fg        #5A3A00;\n"
        "@define-color dnsb_fg_bright #3D2700;\n"
        "@define-color dnsb_fg_dim    #8A6520;\n"
        "@define-color dnsb_fg_faint  alpha(#5A3A00, 0.07);\n"
        "@define-color dnsb_glow      alpha(#5A3A00, 0.18);\n"
        "* { font-family: 'Cascadia Mono','JetBrains Mono','DejaVu Sans Mono',monospace; text-shadow: none; -gtk-icon-shadow: none; }\n"
        "window, dialog { background-color: @dnsb_bg; color: @dnsb_fg; }\n"
        "headerbar { background: @dnsb_bg; color: @dnsb_fg; border-bottom: 1px solid @dnsb_fg; padding: 4px 8px; box-shadow: none; }\n"
        "headerbar .title { color: @dnsb_fg_bright; }\n"
        "headerbar .subtitle { color: @dnsb_fg_dim; }\n"
        "label { color: @dnsb_fg; }\n"
        "button { background: transparent; color: @dnsb_fg_dim; border: 1px solid @dnsb_fg_dim; border-radius: 0; padding: 4px 12px; box-shadow: none; }\n"
        "button label, button image { color: inherit; }\n"
        "button:hover { background: @dnsb_fg; color: @dnsb_bg; border-color: @dnsb_fg; }\n"
        "button:active { background: @dnsb_fg_bright; color: @dnsb_bg; border-color: @dnsb_fg_bright; }\n"
        "button:disabled { color: alpha(@dnsb_fg_dim, 0.4); border-color: alpha(@dnsb_fg_dim, 0.3); background: transparent; }\n"
        "button.suggested-action { background: @dnsb_fg; color: @dnsb_bg; border-color: @dnsb_fg; }\n"
        "button.suggested-action:hover { background: @dnsb_fg_bright; border-color: @dnsb_fg_bright; }\n"
        /* Window-control buttons (minimize / maximize / close) in the headerbar. */
        "headerbar windowcontrols, headerbar windowcontrols box { background: transparent; }\n"
        "button.titlebutton, headerbar windowcontrols button { background: transparent; border: none; padding: 4px 6px; min-width: 22px; min-height: 22px; color: @dnsb_fg; }\n"
        "button.titlebutton image, headerbar windowcontrols button image { color: @dnsb_fg; }\n"
        "button.titlebutton:hover, headerbar windowcontrols button:hover { background: @dnsb_fg_faint; color: @dnsb_fg_bright; border: none; }\n"
        "button.titlebutton:hover image, headerbar windowcontrols button:hover image { color: @dnsb_fg_bright; }\n"
        "button.titlebutton.close:hover, headerbar windowcontrols button.close:hover { background: @dnsb_fg; color: @dnsb_bg; }\n"
        "button.titlebutton.close:hover image, headerbar windowcontrols button.close:hover image { color: @dnsb_bg; }\n"
        "notebook { background: @dnsb_bg; }\n"
        "notebook > header { background: @dnsb_bg; border-color: @dnsb_fg_dim; }\n"
        "notebook tab { background: transparent; color: @dnsb_fg_dim; border-radius: 0; padding: 6px 16px; border: 1px solid transparent; }\n"
        "notebook tab:checked { color: @dnsb_fg_bright; border-color: @dnsb_fg; background: @dnsb_fg_faint; }\n"
        "notebook tab:hover { color: @dnsb_fg; }\n"
        "entry { background: @dnsb_bg; color: @dnsb_fg; border: 1px solid @dnsb_fg_dim; border-radius: 0; padding: 4px 6px; box-shadow: none; }\n"
        "entry:focus { border-color: @dnsb_fg_bright; box-shadow: none; }\n"
        "spinbutton { background: @dnsb_bg; color: @dnsb_fg; border: 1px solid @dnsb_fg_dim; border-radius: 0; padding: 0; box-shadow: none; min-height: 0; }\n"
        "spinbutton:focus-within { border-color: @dnsb_fg_bright; }\n"
        "spinbutton entry { background: transparent; border: none; color: @dnsb_fg; min-height: 0; padding: 4px 6px; box-shadow: none; }\n"
        "spinbutton entry:focus { border: none; box-shadow: none; }\n"
        "spinbutton button { border: none; background: transparent; color: @dnsb_fg; min-width: 22px; min-height: 0; padding: 0 4px; margin: 0; box-shadow: none; }\n"
        "spinbutton button:hover { background: @dnsb_fg; color: @dnsb_bg; }\n"
        "spinbutton button:active { background: @dnsb_fg_bright; color: @dnsb_bg; }\n"
        "spinbutton button:disabled { color: alpha(@dnsb_fg_dim, 0.4); background: transparent; }\n"
        "combobox button { background: @dnsb_bg; color: @dnsb_fg; border: 1px solid @dnsb_fg_dim; padding: 4px 8px; }\n"
        "combobox arrow { color: @dnsb_fg; min-width: 12px; min-height: 12px; }\n"
        "scrollbar button { background: transparent; border: none; color: @dnsb_fg_dim; min-width: 0; min-height: 0; padding: 0; margin: 0; }\n"
        "scrollbar button:hover { background: @dnsb_fg_faint; color: @dnsb_fg; }\n"
        "progressbar > trough { background: @dnsb_bg; border: 1px solid @dnsb_fg_dim; border-radius: 0; }\n"
        "progressbar > trough > progress { background: @dnsb_fg; border-radius: 0; min-height: 4px; }\n"
        "progressbar > text { color: @dnsb_fg_bright; }\n"
        "scrolledwindow, scrolledwindow viewport { background: @dnsb_bg; }\n"
        "scrollbar { background: @dnsb_bg; border: none; }\n"
        "scrollbar slider { background: @dnsb_fg_dim; min-width: 6px; min-height: 6px; border-radius: 0; }\n"
        "scrollbar slider:hover { background: @dnsb_fg; }\n"
        "treeview, treeview.view { background: @dnsb_bg; color: @dnsb_fg; }\n"
        "treeview:selected, treeview.view:selected { background: @dnsb_fg; color: @dnsb_bg; }\n"
        "treeview header button, columnheader button { background: @dnsb_bg; color: @dnsb_fg_dim; border: none; border-bottom: 1px solid @dnsb_fg_dim; border-radius: 0; padding: 4px 8px; }\n"
        "treeview header button:hover, columnheader button:hover { color: @dnsb_fg_bright; background: @dnsb_fg_faint; }\n"
        "textview, textview text { background: @dnsb_bg; color: @dnsb_fg; }\n"
        "textview text selection { background: @dnsb_fg; color: @dnsb_bg; }\n"
        "separator { background: @dnsb_fg_dim; }\n"
        "menu, popover { background: @dnsb_bg; color: @dnsb_fg; border: 1px solid @dnsb_fg; }\n"
        "menuitem { color: @dnsb_fg; padding: 4px 8px; }\n"
        "menuitem:hover { background: @dnsb_fg; color: @dnsb_bg; }\n"
        "tooltip { background: @dnsb_bg; color: @dnsb_fg; border: 1px solid @dnsb_fg; }\n"
        "tooltip label { color: @dnsb_fg; }\n"
        "checkbutton, radiobutton { color: @dnsb_fg; }\n"
        "checkbutton check, radiobutton radio { background: @dnsb_bg; border: 1px solid @dnsb_fg_dim; min-width: 14px; min-height: 14px; box-shadow: none; }\n"
        "checkbutton check:checked, radiobutton radio:checked { background: @dnsb_fg; color: @dnsb_bg; }\n"
        ;
}

static void notify_listeners(void) {
    for (listener *l = g_listeners; l; l = l->next) {
        if (l->cb) l->cb(l->user);
    }
}

void dnsb_theme_apply(dnsb_theme_mode mode) {
    g_mode = mode;
    int dark = (mode == DNSB_THEME_DARK) ? 1
             : (mode == DNSB_THEME_LIGHT) ? 0
             : detect_dark_preferred();

    g_active = dark ? &PALETTE_DARK : &PALETTE_LIGHT;

    /* Hint GTK so any non-themed widgets use the dark variant. */
    g_object_set(gtk_settings_get_default(),
                 "gtk-application-prefer-dark-theme", dark ? TRUE : FALSE,
                 NULL);

    if (!g_provider) {
        g_provider = gtk_css_provider_new();
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
            GTK_STYLE_PROVIDER(g_provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 100);
    }

    GError *err = NULL;
    if (!gtk_css_provider_load_from_data(g_provider, css_for(dark), -1, &err)) {
        DNSB_WARN("theme CSS parse error: %s", err ? err->message : "(unknown)");
        if (err) g_error_free(err);
    }

    save_pref(mode);
    notify_listeners();
}

void dnsb_theme_init(GtkApplication *app) {
    (void)app;
    g_mode = load_pref();
    dnsb_theme_apply(g_mode);
}

dnsb_theme_mode dnsb_theme_get(void) { return g_mode; }
const dnsb_theme_colors *dnsb_theme_colors_current(void) { return g_active; }

void dnsb_theme_register_listener(void (*cb)(void *user), void *user) {
    listener *l = g_new0(listener, 1);
    l->cb = cb;
    l->user = user;
    l->next = g_listeners;
    g_listeners = l;
}

void dnsb_theme_unregister_listener(void (*cb)(void *user), void *user) {
    listener **pp = &g_listeners;
    while (*pp) {
        if ((*pp)->cb == cb && (*pp)->user == user) {
            listener *dead = *pp;
            *pp = dead->next;
            g_free(dead);
            return;
        }
        pp = &(*pp)->next;
    }
}
