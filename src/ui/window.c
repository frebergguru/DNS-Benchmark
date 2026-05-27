#include "window.h"

#include "model_cols.h"
#include "tab_intro.h"
#include "tab_nameservers.h"
#include "tab_tabular.h"
#include "tab_conclusions.h"
#include "tab_help.h"
#include "theme.h"

#include "../engine/engine.h"
#include "../io/csv_export.h"
#include "../net/time_ns.h"
#include "../util/i18n.h"
#include "../util/log.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct dnsb_window {
    GtkApplication *app;
    GtkWidget *root;
    GtkListStore *store;
    GtkWidget *notebook;
    GtkWidget *run_btn;
    GtkWidget *stop_btn;
    GtkWidget *save_btn;
    GtkWidget *settings_btn;
    GtkWidget *theme_btn;
    GtkWidget *lang_btn;
    GtkWidget *progress_bar;
    GtkWidget *header_status;

    dnsb_tab_nameservers ns_tab;
    dnsb_tab_conclusions conc_tab;

    dnsb_engine *engine;
    char *data_dir;

    /* engine_index → row index in store; built when populating. */
    int *row_for_resolver;
    size_t row_for_resolver_n;

    /* Marshal queue. */
    atomic_int idle_pending;

    /* Run timing for progress + ETA. */
    uint64_t run_start_ns;
    double   smoothed_eta_sec;
};

static int find_row_for_index(dnsb_window *w, int engine_idx, GtkTreeIter *out_iter) {
    if (engine_idx < 0 || (size_t)engine_idx >= w->row_for_resolver_n) return 0;
    GtkTreePath *path = gtk_tree_path_new_from_indices(w->row_for_resolver[engine_idx], -1);
    int ok = gtk_tree_model_get_iter(GTK_TREE_MODEL(w->store), out_iter, path);
    gtk_tree_path_free(path);
    return ok;
}

static void refresh_row(dnsb_window *w, size_t i) {
    dnsb_resolver *r = dnsb_engine_resolver_at(w->engine, i);
    if (!r) return;

    GtkTreeIter it;
    if (!find_row_for_index(w, (int)i, &it)) return;

    /* Snapshot under r->stats_mutex so a worker doesn't realloc a samples
       buffer while we read sum/n, or race the status flags. */
    g_mutex_lock(&r->stats_mutex);
    double cached_avg   = dnsb_stats_mean(&r->cached);
    double uncached_avg = dnsb_stats_mean(&r->uncached);
    double dotcom_avg   = dnsb_stats_mean(&r->dotcom);
    int queries_sent = r->queries_sent;
    int queries_ok   = r->queries_ok;
    int sidelined    = r->sidelined;
    int redirects    = r->redirects;
    int dnssec_ok    = r->dnssec_ok;
    g_mutex_unlock(&r->stats_mutex);

    double reliab = queries_sent ? 100.0 * (double)queries_ok / (double)queries_sent : 0.0;

    int status = 0;
    if      (sidelined)         status = -1;
    else if (redirects)         status =  3;
    else if (queries_ok > 0)    status =  1;
    else if (queries_sent > 0)  status =  2;
    else                        status =  0;

    gtk_list_store_set(w->store, &it,
        DNSB_COL_STATUS,        status,
        DNSB_COL_CACHED_MS,     cached_avg,
        DNSB_COL_UNCACHED_MS,   uncached_avg,
        DNSB_COL_DOTCOM_MS,     dotcom_avg,
        DNSB_COL_RELIABILITY,   reliab,
        DNSB_COL_QUERIES_SENT,  queries_sent,
        DNSB_COL_QUERIES_OK,    queries_ok,
        DNSB_COL_REDIRECTS,     redirects ? TRUE : FALSE,
        DNSB_COL_DNSSEC,        dnssec_ok ? TRUE : FALSE,
        -1);
}

static void refresh_all(dnsb_window *w) {
    size_t n = dnsb_engine_resolver_count(w->engine);
    for (size_t i = 0; i < n; i++) refresh_row(w, i);
}

static gchar *build_conclusion_text(dnsb_engine *eng) {
    size_t n = dnsb_engine_resolver_count(eng);
    if (n == 0) return g_strdup("No resolvers tested.\n");

    /* Find best cached & uncached. */
    double best_c = 1e9, best_u = 1e9;
    const char *best_c_name = NULL, *best_u_name = NULL;
    const char *best_c_addr = NULL, *best_u_addr = NULL;
    int sidelined = 0, redirectors = 0, ok = 0;

    for (size_t i = 0; i < n; i++) {
        dnsb_resolver *r = dnsb_engine_resolver_at(eng, i);
        g_mutex_lock(&r->stats_mutex);
        int s_sidelined = r->sidelined;
        int s_redirects = r->redirects;
        int s_ok        = r->queries_ok;
        double mc = dnsb_stats_mean(&r->cached);
        double mu = dnsb_stats_mean(&r->uncached);
        g_mutex_unlock(&r->stats_mutex);

        if (s_sidelined) { sidelined++; continue; }
        if (s_redirects)   redirectors++;
        if (s_ok == 0)     continue;
        ok++;
        if (mc > 0 && mc < best_c) { best_c = mc; best_c_name = r->name; best_c_addr = r->addr; }
        if (mu > 0 && mu < best_u) { best_u = mu; best_u_name = r->name; best_u_addr = r->addr; }
    }

    GString *s = g_string_new(NULL);
    g_string_append(s, "<span size='x-large' weight='bold'>");
    g_string_append(s, _("Benchmark complete"));
    g_string_append(s, "</span>\n\n");

    g_string_append_printf(s,
        _("<b>%zu</b> resolvers tested  ·  "
          "<span foreground='#22aa44'>%d responsive</span>  ·  "
          "<span foreground='#999999'>%d sidelined</span>  ·  "
          "<span foreground='#cc7722'>%d redirectors</span>\n\n"),
        n, ok, sidelined, redirectors);

    if (best_c_name || best_u_name) {
        g_string_append(s, "<b>");
        g_string_append(s, _("Winners"));
        g_string_append(s, "</b>\n");
    }
    if (best_c_name) {
        char *esc_name = g_markup_escape_text(best_c_name, -1);
        char *esc_addr = g_markup_escape_text(best_c_addr, -1);
        g_string_append_printf(s,
            _("  🥇 <span foreground='#cc3333'>Cached</span>:   <b>%s</b> "
              "<span foreground='#888888'>(%s)</span>  —  %.2f ms avg\n"),
            esc_name, esc_addr, best_c);
        g_free(esc_name); g_free(esc_addr);
    }
    if (best_u_name) {
        char *esc_name = g_markup_escape_text(best_u_name, -1);
        char *esc_addr = g_markup_escape_text(best_u_addr, -1);
        g_string_append_printf(s,
            _("  🥇 <span foreground='#22aa44'>Uncached</span>: <b>%s</b> "
              "<span foreground='#888888'>(%s)</span>  —  %.2f ms avg\n"),
            esc_name, esc_addr, best_u);
        g_free(esc_name); g_free(esc_addr);
    }

    g_string_append(s, "\n<b>");
    g_string_append(s, _("Notes"));
    g_string_append(s, "</b>\n");
    g_string_append(s, _(
        "  • Cached numbers best predict everyday browsing.\n"
        "  • Avoid <span foreground='#cc7722'>orange-flagged</span> redirectors — they hijack invalid names.\n"
        "  • Re-run several times; one snapshot is noisy.\n"));

    return g_string_free(s, FALSE);
}

static void format_duration(double seconds, char *out, size_t outlen) {
    if (seconds < 0) seconds = 0;
    int s = (int)(seconds + 0.5);
    int h = s / 3600;
    int m = (s % 3600) / 60;
    int rs = s % 60;
    if (h > 0)      snprintf(out, outlen, "%dh %02dm %02ds", h, m, rs);
    else if (m > 0) snprintf(out, outlen, "%dm %02ds", m, rs);
    else            snprintf(out, outlen, "%ds", rs);
}

static void update_progress_ui(dnsb_window *w) {
    dnsb_engine_config cfg = dnsb_engine_get_config(w->engine);
    /* Per-resolver work that increments queries_sent: 1 warmup uncached query
       + query_sets × 3 (cached + uncached + dotcom). The redirection and
       DNSSEC probes do not bump queries_sent, so they don't belong here. */
    int expected_per = 1 + cfg.query_sets * 3;
    if (expected_per <= 0) expected_per = 1;

    size_t n = dnsb_engine_resolver_count(w->engine);
    if (n == 0) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w->progress_bar), 0.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(w->progress_bar), "No resolvers");
        return;
    }

    double sum = 0.0;
    size_t done = 0;
    for (size_t i = 0; i < n; i++) {
        dnsb_resolver *r = dnsb_engine_resolver_at(w->engine, i);
        g_mutex_lock(&r->stats_mutex);
        int s_sidelined = r->sidelined;
        int sent = r->queries_sent;
        g_mutex_unlock(&r->stats_mutex);
        if (s_sidelined) { sum += 1.0; done++; continue; }
        double p = (double)sent / (double)expected_per;
        if (p > 1.0) p = 1.0;
        if (p >= 1.0) done++;
        sum += p;
    }
    double progress = sum / (double)n;
    if (progress < 0) progress = 0;
    if (progress > 1) progress = 1;

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w->progress_bar), progress);

    double elapsed = (dnsb_now_ns() - w->run_start_ns) / 1.0e9;
    char text[160];

    if (progress >= 1.0) {
        snprintf(text, sizeof(text),
                 _("Finishing…  %zu/%zu resolvers"), done, n);
    } else if (elapsed >= 3.0 && progress >= 0.05) {
        /* Linear extrapolation, then EMA-smoothed so spikes when a batch
           of resolvers finishes don't make the readout flicker. */
        double raw_eta = elapsed * (1.0 - progress) / progress;
        if (w->smoothed_eta_sec <= 0.0) w->smoothed_eta_sec = raw_eta;
        else w->smoothed_eta_sec = 0.70 * w->smoothed_eta_sec + 0.30 * raw_eta;
        char eta_buf[32];
        format_duration(w->smoothed_eta_sec, eta_buf, sizeof(eta_buf));
        snprintf(text, sizeof(text), _("%.1f%%  %zu/%zu resolvers  ·  ETA %s"),
                 progress * 100.0, done, n, eta_buf);
    } else if (progress > 0.0) {
        snprintf(text, sizeof(text), _("%.1f%%  %zu/%zu resolvers  ·  estimating…"),
                 progress * 100.0, done, n);
    } else {
        snprintf(text, sizeof(text), _("Starting…  %zu/%zu resolvers"), done, n);
    }
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(w->progress_bar), text);
}

static gboolean ui_refresh_cb(gpointer data) {
    dnsb_window *w = data;
    atomic_store(&w->idle_pending, 0);
    refresh_all(w);
    dnsb_chart_redraw(w->ns_tab.chart);

    if (dnsb_engine_is_running(w->engine)) {
        update_progress_ui(w);
        /* Live "best so far" summary above the chart. Skipped if too few
           samples landed yet to avoid showing a spurious leader. */
        size_t n = dnsb_engine_resolver_count(w->engine);
        double best_c = 1e9; const char *best_c_name = NULL;
        for (size_t i = 0; i < n; i++) {
            dnsb_resolver *r = dnsb_engine_resolver_at(w->engine, i);
            g_mutex_lock(&r->stats_mutex);
            int ns = (int)dnsb_stats_count(&r->cached);
            double m = dnsb_stats_mean(&r->cached);
            g_mutex_unlock(&r->stats_mutex);
            if (ns >= 3 && m > 0 && m < best_c) {
                best_c = m;
                best_c_name = r->name;
            }
        }
        if (best_c_name) {
            char esc_buf[256];
            char *esc = g_markup_escape_text(best_c_name, -1);
            g_snprintf(esc_buf, sizeof(esc_buf),
                _("<b>Running.</b>  Best so far: <b>%s</b> — %.2f ms cached avg"),
                esc, best_c);
            dnsb_tab_nameservers_set_summary(&w->ns_tab, esc_buf);
            g_free(esc);
        }
    } else {
        gtk_widget_set_sensitive(w->run_btn, TRUE);
        gtk_widget_set_sensitive(w->stop_btn, FALSE);
        gtk_widget_set_sensitive(w->save_btn, TRUE);
        gchar *t = build_conclusion_text(w->engine);
        dnsb_tab_conclusions_set_text(&w->conc_tab, t);
        g_free(t);

        /* Show final elapsed time if a run actually executed. */
        if (w->run_start_ns) {
            char buf[64], dur[32];
            double elapsed = (dnsb_now_ns() - w->run_start_ns) / 1.0e9;
            format_duration(elapsed, dur, sizeof(dur));
            snprintf(buf, sizeof(buf), _("Done in %s"), dur);
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(w->progress_bar), buf);
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w->progress_bar), 1.0);
        } else {
            gtk_progress_bar_set_text(GTK_PROGRESS_BAR(w->progress_bar), _("Idle"));
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w->progress_bar), 0.0);
        }
    }
    return G_SOURCE_REMOVE;
}

static void on_engine_event(const dnsb_event *evt, void *user) {
    dnsb_window *w = user;
    (void)evt;
    /* Coalesce ALL event kinds through the throttle. With the previous
       PROGRESS-only check, RESOLVER_DONE and RUN_DONE bypassed it, leaving
       a narrow race where a worker could queue an idle for w right as
       dnsb_window_destroy was draining the queue. Coalescing here means
       g_idle_remove_by_data(w) under the engine callback_mutex (held while
       a worker calls us) drops anything that's already queued. */
    if (atomic_exchange(&w->idle_pending, 1)) return;
    g_idle_add(ui_refresh_cb, w);
}

static void on_run_clicked(GtkButton *btn, gpointer data) {
    dnsb_window *w = data;
    if (dnsb_engine_is_running(w->engine)) return;

    /* Reset row appearance. */
    GtkTreeIter it;
    if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(w->store), &it)) {
        do {
            gtk_list_store_set(w->store, &it,
                DNSB_COL_STATUS, 0,
                DNSB_COL_CACHED_MS,   0.0,
                DNSB_COL_UNCACHED_MS, 0.0,
                DNSB_COL_DOTCOM_MS,   0.0,
                DNSB_COL_RELIABILITY, 0.0,
                DNSB_COL_QUERIES_SENT, 0,
                DNSB_COL_QUERIES_OK,   0,
                DNSB_COL_REDIRECTS,    FALSE,
                DNSB_COL_DNSSEC,       FALSE,
                -1);
        } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(w->store), &it));
    }

    gtk_widget_set_sensitive(w->run_btn, FALSE);
    gtk_widget_set_sensitive(w->stop_btn, TRUE);
    gtk_widget_set_sensitive(w->save_btn, FALSE);
    w->run_start_ns = dnsb_now_ns();
    w->smoothed_eta_sec = 0.0;
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(w->progress_bar), _("Starting…"));
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w->progress_bar), 0.0);
    dnsb_tab_nameservers_set_summary(&w->ns_tab, _(
        "<b>Benchmark running.</b> Sit tight — results update live."));

    if (dnsb_engine_start(w->engine) != 0) {
        DNSB_ERROR("engine_start failed");
        gtk_widget_set_sensitive(w->run_btn, TRUE);
        gtk_widget_set_sensitive(w->stop_btn, FALSE);
        gtk_widget_set_sensitive(w->save_btn, TRUE);
        return;
    }
    /* Auto-switch to the Nameservers tab so the user sees the chart fill in. */
    gtk_notebook_set_current_page(GTK_NOTEBOOK(w->notebook), 1);
}

static void on_stop_clicked(GtkButton *btn, gpointer data) {
    dnsb_window *w = data;
    dnsb_engine_stop(w->engine);
    g_idle_add(ui_refresh_cb, w);
}

static void on_save_clicked(GtkButton *btn, gpointer data) {
    dnsb_window *w = data;
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        _("Save results as CSV"), GTK_WINDOW(w->root),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("_Save"),   GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), "dnsbenchmark.csv");

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (dnsb_csv_export(path, w->engine) == 0) {
            DNSB_INFO("Saved CSV to %s", path);
        } else {
            DNSB_ERROR("Failed to save CSV to %s", path);
        }
        g_free(path);
    }
    gtk_widget_destroy(dlg);
}

static void on_save_png_clicked(GtkButton *btn, gpointer data) {
    dnsb_window *w = data;
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        _("Save chart as PNG"), GTK_WINDOW(w->root),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("_Save"),   GTK_RESPONSE_ACCEPT, NULL);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dlg), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dlg), "dnsbenchmark.png");

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        if (dnsb_chart_save_png(w->ns_tab.chart, path, 1200) == 0)
            DNSB_INFO("Saved chart PNG to %s", path);
        else
            DNSB_ERROR("Failed to save chart PNG to %s", path);
        g_free(path);
    }
    gtk_widget_destroy(dlg);
}

static GtkListStore *new_list_store(void);

static GtkListStore *clone_store(GtkListStore *src) {
    GtkListStore *dst = new_list_store();
    GtkTreeIter src_it, dst_it;
    if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(src), &src_it)) return dst;
    do {
        GValue v[DNSB_N_COLS] = {{0}};
        int cols[DNSB_N_COLS];
        for (int k = 0; k < DNSB_N_COLS; k++) {
            cols[k] = k;
            gtk_tree_model_get_value(GTK_TREE_MODEL(src), &src_it, k, &v[k]);
        }
        gtk_list_store_append(dst, &dst_it);
        gtk_list_store_set_valuesv(dst, &dst_it, cols, v, DNSB_N_COLS);
        for (int k = 0; k < DNSB_N_COLS; k++) g_value_unset(&v[k]);
    } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(src), &src_it));
    return dst;
}

static void on_clone_clicked(GtkButton *btn, gpointer data) {
    dnsb_window *w = data;
    GtkListStore *snap = clone_store(w->store);
    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(win), _("DNS Benchmark — snapshot"));
    gtk_window_set_default_size(GTK_WINDOW(win), 1000, 600);
    gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(w->root));

    GtkWidget *nb = gtk_notebook_new();
    dnsb_tab_nameservers ns = dnsb_tab_nameservers_new(GTK_TREE_MODEL(snap));
    GtkTreeModel *sort_model = gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(snap));
    GtkWidget *tab = dnsb_tab_tabular_new(sort_model);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), ns.root, gtk_label_new(_("Nameservers")));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), tab,     gtk_label_new(_("Tabular Data")));
    gtk_container_add(GTK_CONTAINER(win), nb);
    /* Snapshot store is owned by widgets via reference counting. */
    g_object_unref(snap);
    gtk_widget_show_all(win);
}

typedef struct {
    GtkWidget *port_spin;
    GtkWidget *host_entry;
    GtkWidget *host_label;
} add_dlg_widgets;

static void on_transport_changed(GtkComboBox *combo, gpointer data) {
    add_dlg_widgets *w = data;
    int idx = gtk_combo_box_get_active(combo);
    int port = (idx == DNSB_TRANSPORT_DOT) ? 853
             : (idx == DNSB_TRANSPORT_DOH) ? 443
             : 53;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w->port_spin), port);
    /* Hostname only matters for DoH (URL) and DoT (SNI/cert). Grey out for
       UDP/TCP so users don't think they have to fill it. */
    gboolean needs_host = (idx == DNSB_TRANSPORT_DOH || idx == DNSB_TRANSPORT_DOT);
    gtk_widget_set_sensitive(w->host_entry, needs_host);
    gtk_widget_set_sensitive(w->host_label, needs_host);
}

static const char *transport_code(int idx) {
    switch (idx) {
        case DNSB_TRANSPORT_TCP: return "tcp";
        case DNSB_TRANSPORT_DOH: return "doh";
        case DNSB_TRANSPORT_DOT: return "dot";
        default:                 return "udp";
    }
}

static void on_add_clicked(GtkButton *btn, gpointer data) {
    dnsb_window *w = data;
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        _("Add resolver"), GTK_WINDOW(w->root), GTK_DIALOG_MODAL,
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("_Add"),    GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_widget_set_margin_top(grid, 14);
    gtk_widget_set_margin_bottom(grid, 14);
    gtk_widget_set_margin_start(grid, 16);
    gtk_widget_set_margin_end(grid, 16);

    GtkWidget *e_name  = gtk_entry_new();
    GtkWidget *e_owner = gtk_entry_new();
    GtkWidget *e_addr  = gtk_entry_new();
    GtkWidget *e_host  = gtk_entry_new();
    GtkWidget *transport_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(transport_combo), "UDP");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(transport_combo), "TCP");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(transport_combo), _("DoH (HTTPS)"));
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(transport_combo), _("DoT (TLS)"));
    gtk_combo_box_set_active(GTK_COMBO_BOX(transport_combo), 0);

    GtkAdjustment *adj_port = gtk_adjustment_new(53, 1, 65535, 1, 100, 0);
    GtkWidget *port_spin = gtk_spin_button_new(adj_port, 1, 0);

    gtk_entry_set_placeholder_text(GTK_ENTRY(e_name),  _("e.g. My Router"));
    gtk_entry_set_placeholder_text(GTK_ENTRY(e_owner), _("e.g. Home"));
    gtk_entry_set_placeholder_text(GTK_ENTRY(e_addr),  _("e.g. 192.168.1.1 or 2606:4700:4700::1111"));
    gtk_entry_set_placeholder_text(GTK_ENTRY(e_host),  _("e.g. cloudflare-dns.com (optional)"));

    gtk_widget_set_tooltip_text(e_addr,
        _("IPv4 or IPv6 literal. For DoH/DoT the request still goes to this IP; the hostname field controls cert validation."));
    gtk_widget_set_tooltip_text(e_host,
        _("Hostname for cert verification (DoT) or HTTPS URL (DoH). Optional — defaults to the IP if blank, which only works when the resolver's certificate covers its IP."));

    GtkWidget *l_host = gtk_label_new(_("Hostname"));
    gtk_label_set_xalign(GTK_LABEL(l_host), 0.0);

    int row = 0;
    GtkWidget *labels[] = {
        gtk_label_new(_("Name")),
        gtk_label_new(_("Owner")),
        gtk_label_new(_("Address")),
        gtk_label_new(_("Transport")),
        gtk_label_new(_("Port")),
        l_host,
    };
    GtkWidget *fields[] = { e_name, e_owner, e_addr, transport_combo, port_spin, e_host };
    for (int i = 0; i < 6; i++) {
        gtk_label_set_xalign(GTK_LABEL(labels[i]), 0.0);
        gtk_grid_attach(GTK_GRID(grid), labels[i], 0, row, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), fields[i], 1, row, 1, 1);
        gtk_widget_set_hexpand(fields[i], TRUE);
        row++;
    }

    add_dlg_widgets ctx = { .port_spin = port_spin, .host_entry = e_host, .host_label = l_host };
    g_signal_connect(transport_combo, "changed", G_CALLBACK(on_transport_changed), &ctx);
    /* Trigger once to set the host-field sensitivity for the default UDP. */
    on_transport_changed(GTK_COMBO_BOX(transport_combo), &ctx);

    gtk_container_add(GTK_CONTAINER(content), grid);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        const char *name  = gtk_entry_get_text(GTK_ENTRY(e_name));
        const char *owner = gtk_entry_get_text(GTK_ENTRY(e_owner));
        const char *addr  = gtk_entry_get_text(GTK_ENTRY(e_addr));
        const char *host  = gtk_entry_get_text(GTK_ENTRY(e_host));
        int tidx = gtk_combo_box_get_active(GTK_COMBO_BOX(transport_combo));
        int port = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(port_spin));

        if (name && *name && addr && *addr) {
            dnsb_resolver *r = g_new0(dnsb_resolver, 1);
            r->name     = g_strdup(name);
            r->owner    = g_strdup(owner ? owner : "");
            r->addr     = g_strdup(addr);
            r->hostname = (host && *host) ? g_strdup(host) : NULL;
            r->transport = (dnsb_transport_kind)tidx;
            r->port = port;
            if (dnsb_engine_add_resolver(w->engine, r) == 0) {
                GtkTreeIter it;
                gtk_list_store_append(w->store, &it);
                gtk_list_store_set(w->store, &it,
                    DNSB_COL_NAME,         r->name,
                    DNSB_COL_OWNER,        r->owner,
                    DNSB_COL_ADDRESS,      r->addr,
                    DNSB_COL_TRANSPORT,    transport_code(tidx),
                    DNSB_COL_STATUS,       0,
                    DNSB_COL_SYSTEM,       FALSE,
                    DNSB_COL_PINNED,       FALSE,
                    DNSB_COL_ENGINE_INDEX, (int)(dnsb_engine_resolver_count(w->engine) - 1),
                    -1);
                int idx = (int)(dnsb_engine_resolver_count(w->engine) - 1);
                w->row_for_resolver = realloc(w->row_for_resolver, (idx + 1) * sizeof(int));
                w->row_for_resolver[idx] = idx;
                w->row_for_resolver_n = idx + 1;
                dnsb_chart_redraw(w->ns_tab.chart);
            } else {
                g_free(r->name); g_free(r->owner); g_free(r->addr); g_free(r->hostname); g_free(r);

                /* Show an error toast so the user knows the address didn't parse. */
                GtkWidget *err = gtk_message_dialog_new(GTK_WINDOW(w->root),
                    GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                    "%s", _("Could not parse that address. Use an IPv4 or IPv6 literal (no hostname)."));
                gtk_dialog_run(GTK_DIALOG(err));
                gtk_widget_destroy(err);
            }
        }
    }
    gtk_widget_destroy(dlg);
}

static void update_theme_button_label(dnsb_window *w) {
    if (!w || !w->theme_btn) return;
    const char *label;
    switch (dnsb_theme_get()) {
        case DNSB_THEME_DARK:  label = _("◐ dark");  break;
        case DNSB_THEME_LIGHT: label = _("◑ light"); break;
        default:               label = _("◐ auto");  break;
    }
    gtk_button_set_label(GTK_BUTTON(w->theme_btn), label);
}

extern void dnsb_app_rebuild_window(void);  /* defined in main.c */

static const char *current_lang_label(void) {
    char *pref = dnsb_lang_pref_load();
    const char *p = pref ? pref : "auto";
    const char *label = "🌐 Auto";
    if      (strcmp(p, "en") == 0) label = "🌐 EN";
    else if (strcmp(p, "nb") == 0) label = "🌐 NB";
    g_free(pref);
    return label;
}

static gboolean rebuild_window_idle(gpointer data) {
    (void)data;
    dnsb_app_rebuild_window();
    return G_SOURCE_REMOVE;
}

static void on_lang_radio_toggled(GtkToggleButton *btn, gpointer data) {
    if (!gtk_toggle_button_get_active(btn)) return;
    const char *code = data;
    char *current = dnsb_lang_pref_load();
    const char *cur = current ? current : "auto";
    if (strcmp(cur, code) == 0) { g_free(current); return; }
    g_free(current);

    dnsb_lang_pref_save(code);
    dnsb_i18n_set_language(code);
    /* Rebuild the window so the new translations show up. Deferred to an
       idle so the popover finishes closing and this signal handler unwinds
       before we tear our own ancestor widget down. */
    g_idle_add(rebuild_window_idle, NULL);
}

static GtkWidget *build_lang_button(dnsb_window *w) {
    GtkWidget *btn = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(btn), current_lang_label());
    gtk_widget_set_tooltip_text(btn, _("Change language"));

    GtkWidget *popover = gtk_popover_new(btn);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);

    GtkWidget *r_auto = gtk_radio_button_new_with_label(NULL, _("Auto (system)"));
    GtkWidget *r_en   = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(r_auto), "English");
    GtkWidget *r_nb   = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(r_auto), "Norsk (bokmål)");

    char *pref = dnsb_lang_pref_load();
    const char *p = pref ? pref : "auto";
    if      (strcmp(p, "en") == 0) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r_en),   TRUE);
    else if (strcmp(p, "nb") == 0) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r_nb),   TRUE);
    else                           gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(r_auto), TRUE);
    g_free(pref);

    /* Connect after setting initial state to avoid spurious relaunches. */
    g_signal_connect(r_auto, "toggled", G_CALLBACK(on_lang_radio_toggled), "auto");
    g_signal_connect(r_en,   "toggled", G_CALLBACK(on_lang_radio_toggled), "en");
    g_signal_connect(r_nb,   "toggled", G_CALLBACK(on_lang_radio_toggled), "nb");

    gtk_box_pack_start(GTK_BOX(box), r_auto, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), r_en,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), r_nb,   FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), sep, FALSE, FALSE, 4);

    GtkWidget *hint = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(hint),
        _("<small>Applies immediately. A running benchmark will stop.</small>"));
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(popover), box);
    gtk_widget_show_all(box);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(btn), popover);

    w->lang_btn = btn;
    return btn;
}

static void on_theme_clicked(GtkButton *btn, gpointer data) {
    dnsb_window *w = data;
    /* Cycle: auto → dark → light → auto */
    dnsb_theme_mode next;
    switch (dnsb_theme_get()) {
        case DNSB_THEME_AUTO:  next = DNSB_THEME_DARK;  break;
        case DNSB_THEME_DARK:  next = DNSB_THEME_LIGHT; break;
        default:               next = DNSB_THEME_AUTO;  break;
    }
    dnsb_theme_apply(next);
    update_theme_button_label(w);
}

static GtkWidget *labeled_row(GtkGrid *grid, int row, const char *text,
                              const char *tooltip, GtkWidget *control) {
    GtkWidget *l = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0);
    gtk_widget_set_hexpand(l, TRUE);
    if (tooltip) {
        gtk_widget_set_tooltip_text(l, tooltip);
        gtk_widget_set_tooltip_text(control, tooltip);
    }
    gtk_grid_attach(grid, l,       0, row, 1, 1);
    gtk_grid_attach(grid, control, 1, row, 1, 1);
    return control;
}

static GtkWidget *framed(const char *title, GtkWidget *inner) {
    GtkWidget *frame = gtk_frame_new(NULL);
    GtkWidget *lbl = gtk_label_new(NULL);
    char buf[64];
    g_snprintf(buf, sizeof(buf), "<b>%s</b>", title);
    gtk_label_set_markup(GTK_LABEL(lbl), buf);
    gtk_frame_set_label_widget(GTK_FRAME(frame), lbl);
    gtk_frame_set_label_align(GTK_FRAME(frame), 0.02, 0.5);
    gtk_widget_set_margin_top(inner, 4);
    gtk_widget_set_margin_bottom(inner, 8);
    gtk_widget_set_margin_start(inner, 12);
    gtk_widget_set_margin_end(inner, 12);
    gtk_container_add(GTK_CONTAINER(frame), inner);
    return frame;
}

static void on_settings_clicked(GtkButton *btn, gpointer data) {
    dnsb_window *w = data;
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        _("Settings"), GTK_WINDOW(w->root), GTK_DIALOG_MODAL,
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("_Apply"),  GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(outer, 14);
    gtk_widget_set_margin_bottom(outer, 14);
    gtk_widget_set_margin_start(outer, 16);
    gtk_widget_set_margin_end(outer, 16);

    /* === Sampling group === */
    GtkWidget *gx1 = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(gx1), 8);
    gtk_grid_set_column_spacing(GTK_GRID(gx1), 12);

    GtkAdjustment *adj_q = gtk_adjustment_new(50, 5, 5000, 5, 25, 0);
    GtkAdjustment *adj_s = gtk_adjustment_new(20, 0, 1000, 5, 25, 0);
    GtkAdjustment *adj_t = gtk_adjustment_new(1500, 100, 10000, 100, 500, 0);
    GtkAdjustment *adj_c = gtk_adjustment_new(32, 1, 500, 1, 8, 0);
    GtkWidget *spin_q = gtk_spin_button_new(adj_q, 1, 0);
    GtkWidget *spin_s = gtk_spin_button_new(adj_s, 1, 0);
    GtkWidget *spin_t = gtk_spin_button_new(adj_t, 1, 0);
    GtkWidget *spin_c = gtk_spin_button_new(adj_c, 1, 0);

    labeled_row(GTK_GRID(gx1), 0, _("Query-sets per resolver"),
        _("Number of cached + uncached + .com triples timed per resolver. Higher = more accurate, slower."),
        spin_q);
    labeled_row(GTK_GRID(gx1), 1, _("Spacing (ms)"),
        _("Delay between query-sets on a single resolver. Higher values reduce burst pressure on the link."),
        spin_s);
    labeled_row(GTK_GRID(gx1), 2, _("Timeout (ms)"),
        _("Per-query wall-clock cap. After this a query counts as a failure."),
        spin_t);
    labeled_row(GTK_GRID(gx1), 3, _("Concurrency"),
        _("Max worker threads. One worker handles one resolver at a time; extras queue."),
        spin_c);

    gtk_box_pack_start(GTK_BOX(outer), framed(_("Sampling"), gx1), FALSE, FALSE, 0);

    /* === Probes group === */
    GtkWidget *probes = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *chk_redirect = gtk_check_button_new_with_label(_("Probe redirection (NXDOMAIN hijack test)"));
    GtkWidget *chk_dnssec   = gtk_check_button_new_with_label(_("Probe DNSSEC (sets DO bit — some resolvers misbehave)"));
    gtk_widget_set_tooltip_text(chk_redirect,
        _("After the main loop, query a random .invalid name. Resolvers returning an A record (instead of NXDOMAIN) get the orange flag."));
    gtk_widget_set_tooltip_text(chk_dnssec,
        _("After the main loop, ask for a known signed domain with the DO bit set and check the AD flag in the reply. Off by default because the DO bit makes a handful of broken resolvers refuse to answer at all."));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chk_redirect), TRUE);
    gtk_box_pack_start(GTK_BOX(probes), chk_redirect, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(probes), chk_dnssec,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(outer), framed(_("Probes"), probes), FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(content), outer);
    gtk_widget_show_all(dlg);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        dnsb_engine_config new_cfg = dnsb_engine_default_config();
        new_cfg.query_sets        = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_q));
        new_cfg.spacing_ms        = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_s));
        new_cfg.timeout_ms        = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_t));
        new_cfg.concurrency       = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin_c));
        new_cfg.probe_redirection = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_redirect));
        new_cfg.probe_dnssec      = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(chk_dnssec));
        dnsb_engine_set_config(w->engine, &new_cfg);
    }
    gtk_widget_destroy(dlg);
}

static GtkListStore *new_list_store(void) {
    return gtk_list_store_new(DNSB_N_COLS,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_INT, G_TYPE_BOOLEAN,
        G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_DOUBLE, G_TYPE_DOUBLE,
        G_TYPE_INT, G_TYPE_INT,
        G_TYPE_BOOLEAN, G_TYPE_BOOLEAN,
        G_TYPE_BOOLEAN,        /* pinned */
        G_TYPE_INT);
}

static const char *transport_str(int t) {
    switch (t) {
        case DNSB_TRANSPORT_UDP: return "udp";
        case DNSB_TRANSPORT_TCP: return "tcp";
        case DNSB_TRANSPORT_DOH: return "doh";
        case DNSB_TRANSPORT_DOT: return "dot";
    }
    return "?";
}

static void populate_store_from_engine(dnsb_window *w) {
    size_t n = dnsb_engine_resolver_count(w->engine);
    free(w->row_for_resolver);
    w->row_for_resolver = calloc(n, sizeof(int));
    w->row_for_resolver_n = n;
    for (size_t i = 0; i < n; i++) {
        dnsb_resolver *r = dnsb_engine_resolver_at(w->engine, i);
        GtkTreeIter it;
        gtk_list_store_append(w->store, &it);
        gtk_list_store_set(w->store, &it,
            DNSB_COL_NAME,        r->name,
            DNSB_COL_OWNER,       r->owner,
            DNSB_COL_ADDRESS,     r->addr,
            DNSB_COL_TRANSPORT,   transport_str(r->transport),
            DNSB_COL_STATUS,      0,
            DNSB_COL_SYSTEM,      r->system_configured ? TRUE : FALSE,
            DNSB_COL_PINNED,      r->pinned ? TRUE : FALSE,
            DNSB_COL_ENGINE_INDEX, (int)i,
            -1);
        w->row_for_resolver[i] = (int)i;
    }
}

/* GAction handlers route keyboard accelerators back into the existing
   button click callbacks so behavior stays in one place. */
static void action_run(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    dnsb_window *w = data;
    if (gtk_widget_get_sensitive(w->run_btn)) on_run_clicked(GTK_BUTTON(w->run_btn), w);
}
static void action_stop(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    dnsb_window *w = data;
    if (gtk_widget_get_sensitive(w->stop_btn)) on_stop_clicked(GTK_BUTTON(w->stop_btn), w);
}
static void action_save_csv(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    dnsb_window *w = data;
    if (gtk_widget_get_sensitive(w->save_btn)) on_save_clicked(GTK_BUTTON(w->save_btn), w);
}
static void action_settings(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    dnsb_window *w = data;
    on_settings_clicked(GTK_BUTTON(w->settings_btn), w);
}
static void action_help(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    dnsb_window *w = data;
    /* Help is the last tab (Introduction=0, Nameservers=1, Tabular=2,
       Conclusions=3, Help=4). */
    gtk_notebook_set_current_page(GTK_NOTEBOOK(w->notebook), 4);
}
static void action_add(GSimpleAction *a, GVariant *p, gpointer data) {
    (void)a; (void)p;
    dnsb_window *w = data;
    on_add_clicked(NULL, w);
}

static const GActionEntry win_actions[] = {
    { "run",      action_run,      NULL, NULL, NULL, {0} },
    { "stop",     action_stop,     NULL, NULL, NULL, {0} },
    { "save-csv", action_save_csv, NULL, NULL, NULL, {0} },
    { "settings", action_settings, NULL, NULL, NULL, {0} },
    { "help",     action_help,     NULL, NULL, NULL, {0} },
    { "add",      action_add,      NULL, NULL, NULL, {0} },
};

static void install_shortcuts(GtkApplication *app, dnsb_window *w) {
    g_action_map_add_action_entries(G_ACTION_MAP(w->root),
                                     win_actions, G_N_ELEMENTS(win_actions), w);
    struct { const char *action; const char *accel; } binds[] = {
        { "win.run",      "<Control>r"      },
        { "win.stop",     "<Control>period" },
        { "win.save-csv", "<Control>s"      },
        { "win.settings", "<Control>comma"  },
        { "win.add",      "<Control>n"      },
        { "win.help",     "F1"              },
    };
    for (size_t i = 0; i < G_N_ELEMENTS(binds); i++) {
        const char *a[] = { binds[i].accel, NULL };
        gtk_application_set_accels_for_action(app, binds[i].action, a);
    }
}

dnsb_window *dnsb_window_new(GtkApplication *app, dnsb_engine *engine, const char *data_dir) {
    dnsb_window *w = g_new0(dnsb_window, 1);
    w->app = app;
    w->engine = engine;
    w->data_dir = g_strdup(data_dir ? data_dir : "");
    atomic_init(&w->idle_pending, 0);

    w->store = new_list_store();

    GtkWidget *win = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(win), "DNS Benchmark");
    gtk_window_set_default_size(GTK_WINDOW(win), 1100, 720);
    w->root = win;
    /* Auto-null w->root when GTK destroys it so dnsb_window_destroy
       doesn't double-destroy a finalised GObject after g_application_run. */
    g_object_add_weak_pointer(G_OBJECT(win), (gpointer *)&w->root);

    gtk_window_set_title(GTK_WINDOW(win), _("DNS Benchmark"));

    /* We use a GtkHeaderBar widget but pack it as a normal child rather than
       a CSD titlebar. Reason: under KDE/Breeze the CSD window-control icons
       are rendered as layered two-tone glyphs that our CSS recolor cannot
       cleanly override. Letting the WM draw its native chrome side-steps
       the issue and keeps the app close button reliable cross-DE. */
    GtkWidget *header = gtk_header_bar_new();
    gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), FALSE);
    gtk_header_bar_set_title(GTK_HEADER_BAR(header), _("DNS Benchmark"));
    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(header), "v" DNSB_VERSION);
    gtk_header_bar_set_has_subtitle(GTK_HEADER_BAR(header), TRUE);

    w->run_btn  = gtk_button_new_with_label(_("Run"));
    w->stop_btn = gtk_button_new_with_label(_("Stop"));
    w->save_btn = gtk_button_new_with_label(_("Save CSV"));
    GtkWidget *png_btn   = gtk_button_new_with_label(_("Save PNG"));
    GtkWidget *clone_btn = gtk_button_new_with_label(_("Clone"));
    GtkWidget *add_btn   = gtk_button_new_with_label(_("Add"));
    w->settings_btn = gtk_button_new_with_label(_("Settings"));
    w->theme_btn = gtk_button_new_with_label(_("◐ auto"));
    gtk_widget_set_tooltip_text(w->theme_btn, _("Cycle theme: auto → dark → light"));
    gtk_style_context_add_class(gtk_widget_get_style_context(w->run_btn), "suggested-action");
    gtk_widget_set_sensitive(w->stop_btn, FALSE);
    g_signal_connect(w->run_btn,      "clicked", G_CALLBACK(on_run_clicked),     w);
    g_signal_connect(w->stop_btn,     "clicked", G_CALLBACK(on_stop_clicked),    w);
    g_signal_connect(w->save_btn,     "clicked", G_CALLBACK(on_save_clicked),    w);
    g_signal_connect(png_btn,         "clicked", G_CALLBACK(on_save_png_clicked),w);
    g_signal_connect(clone_btn,       "clicked", G_CALLBACK(on_clone_clicked),   w);
    g_signal_connect(add_btn,         "clicked", G_CALLBACK(on_add_clicked),     w);
    g_signal_connect(w->settings_btn, "clicked", G_CALLBACK(on_settings_clicked),w);
    g_signal_connect(w->theme_btn,    "clicked", G_CALLBACK(on_theme_clicked),   w);

    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), w->run_btn);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header), w->stop_btn);
    GtkWidget *lang_btn = build_lang_button(w);
    /* pack_end fills right-to-left, so theme_btn is rightmost and lang_btn
       sits immediately to its left, exactly as requested. */
    gtk_header_bar_pack_end  (GTK_HEADER_BAR(header), w->theme_btn);
    gtk_header_bar_pack_end  (GTK_HEADER_BAR(header), lang_btn);
    gtk_header_bar_pack_end  (GTK_HEADER_BAR(header), w->settings_btn);
    gtk_header_bar_pack_end  (GTK_HEADER_BAR(header), w->save_btn);
    gtk_header_bar_pack_end  (GTK_HEADER_BAR(header), png_btn);
    gtk_header_bar_pack_end  (GTK_HEADER_BAR(header), clone_btn);
    gtk_header_bar_pack_end  (GTK_HEADER_BAR(header), add_btn);
    update_theme_button_label(w);
    install_shortcuts(app, w);

    /* Tooltips with accelerator hints. */
    gtk_widget_set_tooltip_markup(w->run_btn,      _("Start benchmark  <b>(Ctrl+R)</b>"));
    gtk_widget_set_tooltip_markup(w->stop_btn,     _("Stop benchmark  <b>(Ctrl+.)</b>"));
    gtk_widget_set_tooltip_markup(w->save_btn,     _("Save results to CSV  <b>(Ctrl+S)</b>"));
    gtk_widget_set_tooltip_markup(add_btn,         _("Add a custom resolver  <b>(Ctrl+N)</b>"));
    gtk_widget_set_tooltip_markup(w->settings_btn, _("Settings  <b>(Ctrl+,)</b>"));
    gtk_widget_set_tooltip_text(png_btn,   _("Save the chart as a PNG image"));
    gtk_widget_set_tooltip_text(clone_btn, _("Open a frozen snapshot of the current results in a new window"));

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    /* Custom amber toolbar (former titlebar) packed inline. */
    gtk_box_pack_start(GTK_BOX(vbox), header, FALSE, FALSE, 0);

    /* Progress bar at the top. */
    w->progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(w->progress_bar), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(w->progress_bar), "Idle");
    gtk_widget_set_margin_top(w->progress_bar, 4);
    gtk_widget_set_margin_bottom(w->progress_bar, 4);
    gtk_widget_set_margin_start(w->progress_bar, 8);
    gtk_widget_set_margin_end(w->progress_bar, 8);
    gtk_box_pack_start(GTK_BOX(vbox), w->progress_bar, FALSE, FALSE, 0);

    /* Notebook with the four tabs. */
    w->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(w->notebook), TRUE);

    /* For nameservers/conclusions we store the structs but only attach .root.
       For tabular we use a sort model so user can re-sort without disturbing
       the engine index lookup. */
    populate_store_from_engine(w);

    GtkTreeModel *chart_sort   = gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(w->store));
    GtkTreeModel *tabular_sort = gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(w->store));

    /* Chart sorts by cached latency, pinned-first. */
    gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(chart_sort), DNSB_COL_CACHED_MS,
                                    dnsb_pin_first_compare,
                                    GINT_TO_POINTER(DNSB_COL_CACHED_MS), NULL);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(chart_sort),
                                         DNSB_COL_CACHED_MS, GTK_SORT_ASCENDING);

    GtkWidget *intro = dnsb_tab_intro_new();
    w->ns_tab   = dnsb_tab_nameservers_new(chart_sort);
    GtkWidget *tabular_root = dnsb_tab_tabular_new(tabular_sort);
    w->conc_tab = dnsb_tab_conclusions_new();

    GtkWidget *help = dnsb_tab_help_new();
    gtk_notebook_append_page(GTK_NOTEBOOK(w->notebook), intro,
                             gtk_label_new(_("Introduction")));
    gtk_notebook_append_page(GTK_NOTEBOOK(w->notebook), w->ns_tab.root,
                             gtk_label_new(_("Nameservers")));
    gtk_notebook_append_page(GTK_NOTEBOOK(w->notebook), tabular_root,
                             gtk_label_new(_("Tabular Data")));
    gtk_notebook_append_page(GTK_NOTEBOOK(w->notebook), w->conc_tab.root,
                             gtk_label_new(_("Conclusions")));
    gtk_notebook_append_page(GTK_NOTEBOOK(w->notebook), help,
                             gtk_label_new(_("Help")));
    /* Start on Introduction so first-time users see the orientation text.
       Clicking Run automatically pivots to the Nameservers tab. */
    gtk_notebook_set_current_page(GTK_NOTEBOOK(w->notebook), 0);

    gtk_box_pack_start(GTK_BOX(vbox), w->notebook, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(win), vbox);

    dnsb_engine_set_callback(w->engine, on_engine_event, w);

    gtk_widget_show_all(win);
    /* Land focus on the Run button so Enter starts a benchmark immediately
       and no other widget shows a focus rectangle on launch. */
    gtk_widget_grab_focus(w->run_btn);
    return w;
}

void dnsb_window_present(dnsb_window *w) {
    gtk_window_present(GTK_WINDOW(w->root));
}

void dnsb_window_destroy(dnsb_window *w) {
    if (!w) return;
    /* Disconnect engine callback so any in-flight events don't bounce into
       the freed window. The caller of rebuild_window will set a new one. */
    dnsb_engine_set_callback(w->engine, NULL, NULL);
    /* Drop any pending g_idle_add(ui_refresh_cb, w) so they don't deref
       the freed struct after rebuild. */
    g_idle_remove_by_data(w);
    /* w->root may already be NULL — the weak pointer set in dnsb_window_new
       auto-clears it when GTK has already destroyed the widget (e.g. via
       the application's close-button path). */
    if (w->root) {
        g_object_remove_weak_pointer(G_OBJECT(w->root), (gpointer *)&w->root);
        gtk_widget_destroy(w->root);
    }
    free(w->row_for_resolver);
    g_free(w->data_dir);
    g_free(w);
}

void dnsb_window_mark_system_resolvers(dnsb_window *w, char **sys, size_t n) {
    if (!w || !sys || !n) return;
    size_t rn = dnsb_engine_resolver_count(w->engine);
    for (size_t i = 0; i < rn; i++) {
        dnsb_resolver *r = dnsb_engine_resolver_at(w->engine, i);
        for (size_t k = 0; k < n; k++) {
            if (sys[k] && strcmp(sys[k], r->addr) == 0) {
                r->system_configured = 1;
                GtkTreeIter it;
                if (find_row_for_index(w, (int)i, &it)) {
                    gtk_list_store_set(w->store, &it, DNSB_COL_SYSTEM, TRUE, -1);
                }
                break;
            }
        }
    }
    dnsb_chart_redraw(w->ns_tab.chart);
}
