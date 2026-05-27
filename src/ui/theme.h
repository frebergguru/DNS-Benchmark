#ifndef DNSB_UI_THEME_H
#define DNSB_UI_THEME_H

#include <gtk/gtk.h>

typedef enum {
    DNSB_THEME_AUTO  = 0,
    DNSB_THEME_DARK,
    DNSB_THEME_LIGHT,
} dnsb_theme_mode;

typedef struct {
    /* All values are 0..1 RGB for direct Cairo use. */
    double bg[3];
    double fg[3];
    double fg_bright[3];
    double fg_dim[3];
    double fg_faint_a;            /* alpha used with fg for very faint overlays */

    /* Bar chart segment colors. */
    double bar_cached[3];
    double bar_uncached[3];
    double bar_dotcom[3];

    /* Status indicators. */
    double status_ok[3];
    double status_fail[3];
    double status_redirect[3];
    double status_sidelined[3];
    double status_idle[3];
    double pin_ring[3];

    int is_dark;                  /* 1 if dark variant is active */
} dnsb_theme_colors;

/* Initialize the theme system. Reads the persisted preference from
   `${XDG_CONFIG_HOME:-~/.config}/DNS-Benchmark/theme` and applies it. */
void dnsb_theme_init(GtkApplication *app);

/* Apply a specific mode (AUTO resolves to dark unless overridden by env). */
void dnsb_theme_apply(dnsb_theme_mode mode);

dnsb_theme_mode dnsb_theme_get(void);
const dnsb_theme_colors *dnsb_theme_colors_current(void);

/* Registered listeners are invoked whenever the active theme changes,
   so custom-drawn widgets (like the chart) can repaint. */
void dnsb_theme_register_listener(void (*cb)(void *user), void *user);

/* Remove the first listener matching (cb, user). Required to avoid use-
   after-free when widgets that registered are destroyed. */
void dnsb_theme_unregister_listener(void (*cb)(void *user), void *user);

/* Returns "auto", "dark", or "light" for display in the UI. */
const char *dnsb_theme_mode_label(dnsb_theme_mode m);

#endif
