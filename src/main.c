#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "engine/engine.h"
#include "io/resolvers_io.h"
#include "net/socket_compat.h"
#include "platform/sysresolvers.h"
#include "ui/theme.h"
#include "ui/window.h"
#include "util/i18n.h"
#include "util/log.h"

typedef struct {
    dnsb_engine *engine;
    dnsb_window *window;
    char        *data_dir;
} app_state;

static char *g_argv0 = NULL;
static app_state *g_state = NULL;

const char *dnsb_app_argv0(void) { return g_argv0; }

/* Rebuild the main window so newly-translated strings appear. Engine state
   (resolver list, in-flight stats, recorded timings) lives outside the
   window and is preserved across the swap. A live benchmark is stopped
   first so worker threads don't hold stale references to the old window. */
void dnsb_app_rebuild_window(void) {
    if (!g_state) return;
    GtkApplication *app = GTK_APPLICATION(g_application_get_default());
    if (!app) return;

    if (dnsb_engine_is_running(g_state->engine))
        dnsb_engine_stop(g_state->engine);

    dnsb_window *old = g_state->window;

    g_state->window = dnsb_window_new(app, g_state->engine, g_state->data_dir);

    char **sys = NULL;
    size_t n_sys = 0;
    if (dnsb_get_system_resolvers(&sys, &n_sys) == 0 && n_sys) {
        dnsb_window_mark_system_resolvers(g_state->window, sys, n_sys);
        for (size_t i = 0; i < n_sys; i++) free(sys[i]);
        free(sys);
    }

    dnsb_window_present(g_state->window);

    if (old) dnsb_window_destroy(old);
}

/* Language switch entry point invoked from the language-popover radio.
   setlocale is not thread-safe, so we must stop and join workers (via
   dnsb_engine_stop) BEFORE mutating LC_ALL / _nl_msg_cat_cntr. */
void dnsb_app_apply_lang_and_rebuild(const char *code) {
    if (!g_state) return;
    if (dnsb_engine_is_running(g_state->engine))
        dnsb_engine_stop(g_state->engine);
    dnsb_i18n_set_language(code);
    dnsb_app_rebuild_window();
}

static int file_exists(const char *p) {
    struct stat st;
    return stat(p, &st) == 0;
}

static char *resolve_data_dir(void) {
    const char *env = g_getenv("DNSB_DATADIR");
    if (env && *env) return g_strdup(env);

    /* AppImage: the runtime sets APPDIR to the extracted root, and our
       data tree lives at $APPDIR/usr/share/dnsbenchmark inside it. Must
       be checked *before* the compile-time install path, which would
       otherwise point at /usr/share/dnsbenchmark on the host system. */
    const char *appdir = g_getenv("APPDIR");
    if (appdir && *appdir) {
        char *dir = g_build_filename(appdir, "usr", "share", "dnsbenchmark", NULL);
        char *probe = g_build_filename(dir, "resolvers_default.tsv", NULL);
        if (file_exists(probe)) { g_free(probe); return dir; }
        g_free(probe); g_free(dir);
    }

#ifdef DNSB_DATADIR_BUILD
    {
        char *p = g_build_filename(DNSB_DATADIR_BUILD, "resolvers_default.tsv", NULL);
        if (file_exists(p)) { g_free(p); return g_strdup(DNSB_DATADIR_BUILD); }
        g_free(p);
    }
#endif
#ifdef DNSB_DATADIR_INSTALL
    {
        char *p = g_build_filename(DNSB_DATADIR_INSTALL, "resolvers_default.tsv", NULL);
        if (file_exists(p)) { g_free(p); return g_strdup(DNSB_DATADIR_INSTALL); }
        g_free(p);
    }
#endif
    return g_strdup(".");
}

static void on_activate(GtkApplication *app, gpointer data) {
    app_state *st = data;
    g_state = st;

    dnsb_theme_init(app);

    char *resolvers_path = g_build_filename(st->data_dir, "resolvers_default.tsv", NULL);
    char *domains_path   = g_build_filename(st->data_dir, "test_domains.txt", NULL);

    int n_loaded = dnsb_load_resolvers_tsv(resolvers_path, st->engine);
    if (n_loaded <= 0) {
        DNSB_WARN("no resolvers loaded from %s; add some via the Add button", resolvers_path);
    } else {
        DNSB_INFO("loaded %d resolvers from %s", n_loaded, resolvers_path);
    }

    char **domains = NULL;
    size_t n_domains = 0;
    if (dnsb_load_domains_txt(domains_path, &domains, &n_domains) > 0) {
        dnsb_engine_set_domains(st->engine, (const char **)domains, n_domains);
        for (size_t i = 0; i < n_domains; i++) g_free(domains[i]);
        g_free(domains);
    } else {
        const char *fallback[] = { "google.com", "youtube.com", "wikipedia.org", "facebook.com" };
        dnsb_engine_set_domains(st->engine, fallback, 4);
    }

    g_free(resolvers_path);
    g_free(domains_path);

    st->window = dnsb_window_new(app, st->engine, st->data_dir);

    char **sys = NULL;
    size_t n_sys = 0;
    if (dnsb_get_system_resolvers(&sys, &n_sys) == 0 && n_sys) {
        dnsb_window_mark_system_resolvers(st->window, sys, n_sys);
        for (size_t i = 0; i < n_sys; i++) free(sys[i]);
        free(sys);
    }

    dnsb_window_present(st->window);
}

int main(int argc, char **argv) {
    g_argv0 = g_strdup(argv[0]);

    dnsb_log_set_level(DNSB_LOG_INFO);
    /* Apply any persisted language override BEFORE gettext init so setlocale
       and bindtextdomain pick it up. */
    dnsb_lang_apply_pref_env();
    dnsb_i18n_init(NULL);

    if (dnsb_net_init() != 0) {
        fprintf(stderr, "Failed to initialize networking\n");
        return 1;
    }

    app_state st = {0};
    st.engine = dnsb_engine_new();
    if (!st.engine) {
        fprintf(stderr, "Failed to allocate engine\n");
        dnsb_net_shutdown();
        return 1;
    }
    st.data_dir = resolve_data_dir();
    DNSB_INFO("data dir: %s", st.data_dir);

    /* G_APPLICATION_DEFAULT_FLAGS replaced G_APPLICATION_FLAGS_NONE in
       GLib 2.74. Pick the right one based on the GLib we're compiling
       against so neither modern nor Ubuntu-22.04 (GLib 2.72) builds warn. */
#if GLIB_CHECK_VERSION(2, 74, 0)
    GtkApplication *app = gtk_application_new("guru.freberg.DNSBenchmark", G_APPLICATION_DEFAULT_FLAGS);
#else
    GtkApplication *app = gtk_application_new("guru.freberg.DNSBenchmark", G_APPLICATION_FLAGS_NONE);
#endif
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), &st);
    int rc = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);

    dnsb_window_destroy(st.window);
    dnsb_engine_free(st.engine);
    g_free(st.data_dir);
    dnsb_net_shutdown();
    return rc;
}
