#include "chart.h"
#include "model_cols.h"
#include "theme.h"

#include <math.h>
#include <stdio.h>

#define ROW_HEIGHT       28
#define LABEL_WIDTH     180
#define LEFT_PAD          8
#define RIGHT_PAD        12
#define BAR_GROUP_GAP     6
#define TICK_PAD         16

struct dnsb_chart {
    GtkTreeModel *model;
    GtkWidget    *area;
    double        lock_scale_ms;     /* 0 = auto-scale */
};

static double model_max_ms(GtkTreeModel *m) {
    double best = 0;
    GtkTreeIter it;
    if (!gtk_tree_model_get_iter_first(m, &it)) return 1.0;
    do {
        double cv, u, d;
        gtk_tree_model_get(m, &it,
            DNSB_COL_CACHED_MS,   &cv,
            DNSB_COL_UNCACHED_MS, &u,
            DNSB_COL_DOTCOM_MS,   &d, -1);
        if (cv > best) best = cv;
        if (u > best)  best = u;
        if (d > best)  best = d;
    } while (gtk_tree_model_iter_next(m, &it));
    return best > 0 ? best : 1.0;
}

static int row_count(GtkTreeModel *m) {
    return gtk_tree_model_iter_n_children(m, NULL);
}

static int needed_height(GtkTreeModel *m) {
    int n = row_count(m);
    return TICK_PAD + 8 + n * ROW_HEIGHT + 4;
}

static void use_rgb(cairo_t *cr, const double rgb[3], double a) {
    cairo_set_source_rgba(cr, rgb[0], rgb[1], rgb[2], a);
}

static void draw_chart_to(cairo_t *cr, GtkTreeModel *model, double lock_scale,
                          int width, int height) {
    (void)height;
    const dnsb_theme_colors *t = dnsb_theme_colors_current();

    use_rgb(cr, t->bg, 1.0);
    cairo_paint(cr);

    int n = row_count(model);
    if (n == 0) {
        /* Centered empty-state with a friendly hint. */
        use_rgb(cr, t->fg_dim, 1.0);
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 16);
        const char *msg = "No resolvers loaded";
        cairo_text_extents_t te;
        cairo_text_extents(cr, msg, &te);
        cairo_move_to(cr, (width - te.width) / 2.0, 60);
        cairo_show_text(cr, msg);

        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_ITALIC, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 11);
        const char *hint = "Use the Add button (Ctrl+N) or edit data/resolvers_default.tsv";
        cairo_text_extents(cr, hint, &te);
        cairo_move_to(cr, (width - te.width) / 2.0, 80);
        cairo_show_text(cr, hint);
        return;
    }

    double max_ms = lock_scale > 0 ? lock_scale : model_max_ms(model);
    int top_pad = TICK_PAD + 8;

    int chart_left = LEFT_PAD + LABEL_WIDTH;
    int chart_right = width - RIGHT_PAD;
    int chart_w = chart_right - chart_left;
    if (chart_w < 50) chart_w = 50;

    /* Top guide line. */
    use_rgb(cr, t->fg_dim, 0.4);
    cairo_set_line_width(cr, 1.0);
    cairo_move_to(cr, chart_left, top_pad - 2);
    cairo_line_to(cr, chart_right, top_pad - 2);
    cairo_stroke(cr);

    /* Vertical gridlines at sensible intervals. */
    double tick_step = 10.0;
    while (tick_step * 10 < max_ms) tick_step *= 2;
    if (tick_step < 5)  tick_step = 5;
    /* No more than ~8 gridlines: aim for that. */
    while (max_ms / tick_step > 8) tick_step *= 2;

    int row_count_for_grid = n;
    int chart_bottom = top_pad + row_count_for_grid * ROW_HEIGHT;
    use_rgb(cr, t->fg_dim, 0.18);
    cairo_set_line_width(cr, 1.0);
    for (double v = tick_step; v < max_ms; v += tick_step) {
        double x = chart_left + (v / max_ms) * chart_w;
        cairo_move_to(cr, x, top_pad - 2);
        cairo_line_to(cr, x, chart_bottom);
        cairo_stroke(cr);
    }

    /* Axis labels at top: 0 ms, mid tick, max ms. */
    use_rgb(cr, t->fg_dim, 1.0);
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 9);
    cairo_text_extents_t te;
    cairo_move_to(cr, chart_left, top_pad - 6);
    cairo_show_text(cr, "0 ms");
    /* mid tick label so it's clear gridlines aren't equispaced visually */
    char midbuf[24];
    double mid = tick_step * (int)(max_ms / (2 * tick_step));
    if (mid >= tick_step) {
        snprintf(midbuf, sizeof(midbuf), "%g ms", mid);
        cairo_text_extents(cr, midbuf, &te);
        double mx = chart_left + (mid / max_ms) * chart_w;
        cairo_move_to(cr, mx - te.width / 2, top_pad - 6);
        cairo_show_text(cr, midbuf);
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%.0f ms", max_ms);
    cairo_text_extents(cr, buf, &te);
    cairo_move_to(cr, chart_right - te.width, top_pad - 6);
    cairo_show_text(cr, buf);

    GtkTreeIter it;
    if (!gtk_tree_model_get_iter_first(model, &it)) return;

    int row = 0;
    do {
        gchar *name = NULL;
        gchar *addr = NULL;
        double mc = 0, mu = 0, md = 0;
        int status = 0;
        gboolean system = FALSE, pinned = FALSE;
        gtk_tree_model_get(model, &it,
            DNSB_COL_NAME, &name,
            DNSB_COL_ADDRESS, &addr,
            DNSB_COL_CACHED_MS, &mc,
            DNSB_COL_UNCACHED_MS, &mu,
            DNSB_COL_DOTCOM_MS, &md,
            DNSB_COL_STATUS, &status,
            DNSB_COL_SYSTEM, &system,
            DNSB_COL_PINNED, &pinned,
            -1);

        int y0 = top_pad + row * ROW_HEIGHT;

        /* Status dot. */
        double cx = LEFT_PAD + 8, cy = y0 + ROW_HEIGHT / 2;
        double rad = 5;
        const double *dot = t->status_idle;
        if      (status ==  2) dot = t->status_fail;
        else if (status ==  3) dot = t->status_redirect;
        else if (status == -1) dot = t->status_sidelined;
        else if (status ==  1) dot = t->status_ok;
        use_rgb(cr, dot, 1.0);
        if (system) {
            cairo_arc(cr, cx, cy, rad, 0, 2 * M_PI);
            cairo_fill(cr);
        } else {
            cairo_set_line_width(cr, 1.6);
            cairo_arc(cr, cx, cy, rad, 0, 2 * M_PI);
            cairo_stroke(cr);
        }
        if (pinned) {
            use_rgb(cr, t->pin_ring, 1.0);
            cairo_set_line_width(cr, 1.4);
            cairo_arc(cr, cx, cy, rad + 3, 0, 2 * M_PI);
            cairo_stroke(cr);
        }

        use_rgb(cr, t->fg, 1.0);
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 11);
        cairo_move_to(cr, LEFT_PAD + 22, y0 + 12);
        cairo_show_text(cr, name ? name : "(unnamed)");

        use_rgb(cr, t->fg_dim, 1.0);
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 9);
        cairo_move_to(cr, LEFT_PAD + 22, y0 + 24);
        cairo_show_text(cr, addr ? addr : "");

        int bar_h = (ROW_HEIGHT - BAR_GROUP_GAP) / 3;
        int by = y0 + 2;

        double ws[3] = {
            mc > 0 ? (mc / max_ms) * chart_w : 0,
            mu > 0 ? (mu / max_ms) * chart_w : 0,
            md > 0 ? (md / max_ms) * chart_w : 0,
        };

        const double *bar_colors[3] = {
            t->bar_cached, t->bar_uncached, t->bar_dotcom
        };

        for (int k = 0; k < 3; k++) {
            /* Faint baseline so empty bars are still visible. */
            use_rgb(cr, t->fg, t->fg_faint_a);
            cairo_rectangle(cr, chart_left, by + bar_h * k, chart_w, bar_h);
            cairo_fill(cr);

            if (ws[k] > 0) {
                use_rgb(cr, bar_colors[k], 1.0);
                cairo_rectangle(cr, chart_left, by + bar_h * k, ws[k], bar_h);
                cairo_fill(cr);
            }
        }

        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 9);
        use_rgb(cr, t->fg, 1.0);
        double vals[3] = { mc, mu, md };
        for (int k = 0; k < 3; k++) {
            if (vals[k] <= 0) continue;
            char nbuf[24];
            snprintf(nbuf, sizeof(nbuf), "%.1f", vals[k]);
            cairo_text_extents(cr, nbuf, &te);
            double tx = chart_left + ws[k] + 4;
            if (tx + te.width > chart_right) tx = chart_left + ws[k] - te.width - 4;
            cairo_move_to(cr, tx, by + bar_h * k + bar_h - 1);
            cairo_show_text(cr, nbuf);
        }

        g_free(name);
        g_free(addr);
        row++;
    } while (gtk_tree_model_iter_next(model, &it));
}

static gboolean on_draw(GtkWidget *w, cairo_t *cr, gpointer data) {
    dnsb_chart *c = data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(w, &alloc);
    int h = needed_height(c->model);
    gtk_widget_set_size_request(w, -1, h);
    draw_chart_to(cr, c->model, c->lock_scale_ms, alloc.width, alloc.height);
    return FALSE;
}

static void on_model_changed(GtkTreeModel *m, GtkTreePath *p, GtkTreeIter *it, gpointer data) {
    (void)m; (void)p; (void)it;
    dnsb_chart_redraw(data);
}

static void on_model_reordered(GtkTreeModel *m, GtkTreePath *p, GtkTreeIter *it, gpointer arg, gpointer data) {
    (void)m; (void)p; (void)it; (void)arg;
    dnsb_chart_redraw(data);
}

static void on_theme_changed(void *user) {
    dnsb_chart_redraw(user);
}

static void on_chart_destroyed(GtkWidget *area, gpointer data) {
    (void)area;
    dnsb_chart *c = data;
    if (!c) return;
    /* Unregister BOTH the theme listener and the model signal handlers
       so events arriving after destruction don't dereference the freed
       struct. */
    dnsb_theme_unregister_listener(on_theme_changed, c);
    if (c->model) {
        g_signal_handlers_disconnect_by_data(c->model, c);
    }
    c->area = NULL;
    g_free(c);
}

dnsb_chart *dnsb_chart_new(GtkTreeModel *model) {
    dnsb_chart *c = g_new0(dnsb_chart, 1);
    c->model = model;
    c->area = gtk_drawing_area_new();
    gtk_widget_set_size_request(c->area, 480, 200);
    g_signal_connect(c->area, "draw",    G_CALLBACK(on_draw),           c);
    g_signal_connect(c->area, "destroy", G_CALLBACK(on_chart_destroyed), c);
    g_signal_connect(model, "row-changed",    G_CALLBACK(on_model_changed),  c);
    g_signal_connect(model, "row-inserted",   G_CALLBACK(on_model_changed),  c);
    g_signal_connect(model, "row-deleted",    G_CALLBACK(on_model_changed),  c);
    g_signal_connect(model, "rows-reordered", G_CALLBACK(on_model_reordered), c);
    dnsb_theme_register_listener(on_theme_changed, c);
    return c;
}

GtkWidget *dnsb_chart_widget(dnsb_chart *c) { return c->area; }

void dnsb_chart_redraw(dnsb_chart *c) {
    if (c && c->area) gtk_widget_queue_draw(c->area);
}

void dnsb_chart_lock_scale(dnsb_chart *c, double max_ms_or_zero) {
    if (c) c->lock_scale_ms = max_ms_or_zero;
    dnsb_chart_redraw(c);
}

int dnsb_chart_save_png(dnsb_chart *c, const char *path, int width) {
    if (!c) return -1;
    if (width <= 0) width = 1100;
    int h = needed_height(c->model);
    cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, h);
    if (!surf) return -1;
    cairo_t *cr = cairo_create(surf);
    draw_chart_to(cr, c->model, c->lock_scale_ms, width, h);
    cairo_destroy(cr);
    cairo_status_t st = cairo_surface_write_to_png(surf, path);
    cairo_surface_destroy(surf);
    return st == CAIRO_STATUS_SUCCESS ? 0 : -1;
}
