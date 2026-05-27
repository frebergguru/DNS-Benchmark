#include "tab_tabular.h"
#include "model_cols.h"

#include "../util/i18n.h"

#include <stdio.h>
#include <string.h>

int dnsb_pin_first_compare(GtkTreeModel *m, GtkTreeIter *a, GtkTreeIter *b, gpointer data) {
    int col = GPOINTER_TO_INT(data);
    gboolean pa = FALSE, pb = FALSE;
    gtk_tree_model_get(m, a, DNSB_COL_PINNED, &pa, -1);
    gtk_tree_model_get(m, b, DNSB_COL_PINNED, &pb, -1);
    if (pa != pb) return pa ? -1 : 1;

    GType t = gtk_tree_model_get_column_type(m, col);
    if (t == G_TYPE_DOUBLE) {
        double da, db;
        gtk_tree_model_get(m, a, col, &da, -1);
        gtk_tree_model_get(m, b, col, &db, -1);
        /* Treat 0.0 (idle) as max so it sorts to the bottom on ascending. */
        if (da == 0.0) da = 1e9;
        if (db == 0.0) db = 1e9;
        return (da > db) - (da < db);
    }
    if (t == G_TYPE_INT) {
        int ia, ib;
        gtk_tree_model_get(m, a, col, &ia, -1);
        gtk_tree_model_get(m, b, col, &ib, -1);
        /* (ia - ib) overflows for unbounded ints (queries_sent/queries_ok). */
        return (ia > ib) - (ia < ib);
    }
    if (t == G_TYPE_STRING) {
        gchar *sa = NULL, *sb = NULL;
        gtk_tree_model_get(m, a, col, &sa, -1);
        gtk_tree_model_get(m, b, col, &sb, -1);
        int r = g_strcmp0(sa, sb);
        g_free(sa); g_free(sb);
        return r;
    }
    if (t == G_TYPE_BOOLEAN) {
        gboolean ba, bb;
        gtk_tree_model_get(m, a, col, &ba, -1);
        gtk_tree_model_get(m, b, col, &bb, -1);
        return ba - bb;
    }
    return 0;
}

static void cell_ms_func(GtkTreeViewColumn *col, GtkCellRenderer *r,
                         GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
    int col_id = GPOINTER_TO_INT(data);
    double v;
    gtk_tree_model_get(model, iter, col_id, &v, -1);
    char buf[32];
    if (v > 0.0) snprintf(buf, sizeof(buf), "%.2f", v);
    else         snprintf(buf, sizeof(buf), "%s", "—");
    g_object_set(r, "text", buf, NULL);
}

static void cell_pct_func(GtkTreeViewColumn *col, GtkCellRenderer *r,
                          GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
    int col_id = GPOINTER_TO_INT(data);
    double v;
    gtk_tree_model_get(model, iter, col_id, &v, -1);
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f%%", v);
    g_object_set(r, "text", buf, NULL);
}

static void cell_bool_func(GtkTreeViewColumn *col, GtkCellRenderer *r,
                           GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
    int col_id = GPOINTER_TO_INT(data);
    gboolean v = FALSE;
    gtk_tree_model_get(model, iter, col_id, &v, -1);
    g_object_set(r, "text", v ? _("yes") : "—", NULL);
}

static void cell_status_func(GtkTreeViewColumn *col, GtkCellRenderer *r,
                             GtkTreeModel *model, GtkTreeIter *iter, gpointer data) {
    (void)col; (void)data;
    int status = 0;
    gboolean system = FALSE;
    gtk_tree_model_get(model, iter, DNSB_COL_STATUS, &status, DNSB_COL_SYSTEM, &system, -1);
    const char *s;
    switch (status) {
        case  1: s = system ? _("● ok")       : _("○ ok");       break;
        case  2: s = system ? _("● fail")     : _("○ fail");     break;
        case  3: s = system ? _("● redirect") : _("○ redirect"); break;
        case -1: s = _("sidelined");                              break;
        default: s = system ? _("● idle")     : _("○ idle");     break;
    }
    g_object_set(r, "text", s, NULL);
}

typedef struct { GtkTreeModel *src_store; } pin_toggle_ctx;

static void on_pin_toggled(GtkCellRendererToggle *r, gchar *path_str, gpointer data) {
    GtkTreeModel *sort_model = data;
    GtkTreePath *sort_path = gtk_tree_path_new_from_string(path_str);
    GtkTreeIter sort_iter;
    if (!gtk_tree_model_get_iter(sort_model, &sort_iter, sort_path)) {
        gtk_tree_path_free(sort_path);
        return;
    }
    GtkTreeIter child_iter;
    gtk_tree_model_sort_convert_iter_to_child_iter(GTK_TREE_MODEL_SORT(sort_model),
                                                    &child_iter, &sort_iter);
    GtkTreeModel *child_model = gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(sort_model));
    gboolean was = FALSE;
    gtk_tree_model_get(child_model, &child_iter, DNSB_COL_PINNED, &was, -1);
    gtk_list_store_set(GTK_LIST_STORE(child_model), &child_iter,
                       DNSB_COL_PINNED, !was, -1);
    gtk_tree_path_free(sort_path);
}

static void add_pin_col(GtkTreeView *tv, GtkTreeModel *sort_model) {
    GtkCellRenderer *r = gtk_cell_renderer_toggle_new();
    g_signal_connect(r, "toggled", G_CALLBACK(on_pin_toggled), sort_model);
    GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(
        _("Pin"), r, "active", DNSB_COL_PINNED, NULL);
    gtk_tree_view_column_set_resizable(c, FALSE);
    gtk_tree_view_column_set_min_width(c, 40);
    gtk_tree_view_append_column(tv, c);
}

static void add_text_col(GtkTreeView *tv, const char *title, int col_id, int min_w) {
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(title, r, "text", col_id, NULL);
    gtk_tree_view_column_set_sort_column_id(c, col_id);
    gtk_tree_view_column_set_resizable(c, TRUE);
    gtk_tree_view_column_set_min_width(c, min_w);
    gtk_tree_view_append_column(tv, c);
}

static void add_func_col(GtkTreeView *tv, const char *title, int sort_col,
                         GtkTreeCellDataFunc fn, int data_col, int min_w, int monospace) {
    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    if (monospace) g_object_set(r, "family", "monospace", "xalign", 1.0, NULL);
    GtkTreeViewColumn *c = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(c, title);
    gtk_tree_view_column_pack_start(c, r, TRUE);
    gtk_tree_view_column_set_cell_data_func(c, r, fn, GINT_TO_POINTER(data_col), NULL);
    gtk_tree_view_column_set_sort_column_id(c, sort_col);
    gtk_tree_view_column_set_resizable(c, TRUE);
    gtk_tree_view_column_set_min_width(c, min_w);
    gtk_tree_view_append_column(tv, c);
}

/* Context-menu actions on the right-click target row. */
static void copy_address_to_clipboard(GtkTreeView *tv) {
    GtkTreeSelection *sel = gtk_tree_view_get_selection(tv);
    GtkTreeModel *m = NULL;
    GtkTreeIter it;
    if (!gtk_tree_selection_get_selected(sel, &m, &it)) return;
    gchar *addr = NULL;
    gtk_tree_model_get(m, &it, DNSB_COL_ADDRESS, &addr, -1);
    if (addr && *addr) {
        GtkClipboard *cb = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_text(cb, addr, -1);
    }
    g_free(addr);
}

static void toggle_pin_selected(GtkTreeView *tv) {
    GtkTreeSelection *sel = gtk_tree_view_get_selection(tv);
    GtkTreeModel *sort_model = NULL;
    GtkTreeIter sort_it;
    if (!gtk_tree_selection_get_selected(sel, &sort_model, &sort_it)) return;
    GtkTreeIter child_it;
    gtk_tree_model_sort_convert_iter_to_child_iter(GTK_TREE_MODEL_SORT(sort_model),
                                                    &child_it, &sort_it);
    GtkTreeModel *child = gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(sort_model));
    gboolean cur = FALSE;
    gtk_tree_model_get(child, &child_it, DNSB_COL_PINNED, &cur, -1);
    gtk_list_store_set(GTK_LIST_STORE(child), &child_it, DNSB_COL_PINNED, !cur, -1);
}

static void on_ctx_copy(GtkMenuItem *m, gpointer data) { (void)m; copy_address_to_clipboard(GTK_TREE_VIEW(data)); }
static void on_ctx_pin (GtkMenuItem *m, gpointer data) { (void)m; toggle_pin_selected(GTK_TREE_VIEW(data));     }

static gboolean on_tv_button_press(GtkWidget *widget, GdkEventButton *evt, gpointer data) {
    (void)data;
    if (evt->type != GDK_BUTTON_PRESS || evt->button != GDK_BUTTON_SECONDARY) return FALSE;

    /* Select the row under the cursor so the context menu acts on it. */
    GtkTreeView *tv = GTK_TREE_VIEW(widget);
    GtkTreePath *path = NULL;
    if (gtk_tree_view_get_path_at_pos(tv, (int)evt->x, (int)evt->y,
                                       &path, NULL, NULL, NULL)) {
        gtk_tree_selection_select_path(gtk_tree_view_get_selection(tv), path);
        gtk_tree_path_free(path);
    } else {
        return FALSE;
    }

    GtkWidget *menu = gtk_menu_new();
    GtkWidget *m_pin  = gtk_menu_item_new_with_label(_("Toggle pin"));
    GtkWidget *m_copy = gtk_menu_item_new_with_label(_("Copy address"));
    g_signal_connect(m_pin,  "activate", G_CALLBACK(on_ctx_pin),  widget);
    g_signal_connect(m_copy, "activate", G_CALLBACK(on_ctx_copy), widget);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), m_pin);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), m_copy);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (const GdkEvent *)evt);
    /* Free the menu when it's dismissed. */
    g_signal_connect(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), NULL);
    return TRUE;
}

GtkWidget *dnsb_tab_tabular_new(GtkTreeModel *model) {
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget *tv = gtk_tree_view_new_with_model(model);
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tv), TRUE);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(tv), GTK_TREE_VIEW_GRID_LINES_HORIZONTAL);
    g_signal_connect(tv, "button-press-event", G_CALLBACK(on_tv_button_press), NULL);

    add_pin_col(GTK_TREE_VIEW(tv), model);
    add_text_col(GTK_TREE_VIEW(tv), _("Name"),      DNSB_COL_NAME,      160);
    add_text_col(GTK_TREE_VIEW(tv), _("Owner"),     DNSB_COL_OWNER,     120);
    add_text_col(GTK_TREE_VIEW(tv), _("Address"),   DNSB_COL_ADDRESS,   150);
    add_text_col(GTK_TREE_VIEW(tv), _("Transport"), DNSB_COL_TRANSPORT,  70);
    add_func_col(GTK_TREE_VIEW(tv), _("Status"),    DNSB_COL_STATUS, cell_status_func, 0, 90, 0);
    add_func_col(GTK_TREE_VIEW(tv), _("Cached ms"),     DNSB_COL_CACHED_MS,   cell_ms_func,   DNSB_COL_CACHED_MS,   90, 1);
    add_func_col(GTK_TREE_VIEW(tv), _("Uncached ms"),   DNSB_COL_UNCACHED_MS, cell_ms_func,   DNSB_COL_UNCACHED_MS, 100, 1);
    add_func_col(GTK_TREE_VIEW(tv), _(".com ms"),       DNSB_COL_DOTCOM_MS,   cell_ms_func,   DNSB_COL_DOTCOM_MS,    80, 1);
    add_func_col(GTK_TREE_VIEW(tv), _("Reliability"),   DNSB_COL_RELIABILITY, cell_pct_func,  DNSB_COL_RELIABILITY,  90, 1);
    add_text_col(GTK_TREE_VIEW(tv), _("Sent"),      DNSB_COL_QUERIES_SENT,  60);
    add_text_col(GTK_TREE_VIEW(tv), _("OK"),        DNSB_COL_QUERIES_OK,    60);
    add_func_col(GTK_TREE_VIEW(tv), _("Redirects"), DNSB_COL_REDIRECTS, cell_bool_func, DNSB_COL_REDIRECTS, 80, 0);
    add_func_col(GTK_TREE_VIEW(tv), _("DNSSEC"),    DNSB_COL_DNSSEC,    cell_bool_func, DNSB_COL_DNSSEC,    80, 0);

    /* Pin-aware sort for every sortable column. */
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(model);
    int sortable_cols[] = {
        DNSB_COL_NAME, DNSB_COL_OWNER, DNSB_COL_ADDRESS, DNSB_COL_TRANSPORT,
        DNSB_COL_STATUS, DNSB_COL_CACHED_MS, DNSB_COL_UNCACHED_MS, DNSB_COL_DOTCOM_MS,
        DNSB_COL_RELIABILITY, DNSB_COL_QUERIES_SENT, DNSB_COL_QUERIES_OK,
        DNSB_COL_REDIRECTS, DNSB_COL_DNSSEC,
    };
    for (size_t k = 0; k < sizeof(sortable_cols) / sizeof(sortable_cols[0]); k++) {
        gtk_tree_sortable_set_sort_func(sortable, sortable_cols[k],
                                        dnsb_pin_first_compare,
                                        GINT_TO_POINTER(sortable_cols[k]), NULL);
    }
    gtk_tree_sortable_set_sort_column_id(sortable, DNSB_COL_CACHED_MS, GTK_SORT_ASCENDING);

    gtk_container_add(GTK_CONTAINER(scroll), tv);
    gtk_widget_show_all(scroll);
    return scroll;
}
