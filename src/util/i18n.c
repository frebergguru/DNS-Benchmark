#include "i18n.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <glib.h>

static int dir_has_messages(const char *dir) {
    if (!dir || !*dir) return 0;
    struct stat st;
    return stat(dir, &st) == 0;
}

void dnsb_i18n_init(const char *locale_dir) {
    setlocale(LC_ALL, "");

    /* Static so we can return its address as `dir` without leaking — the
       value is read by bindtextdomain inside this call and not used after. */
    static char appdir_locale[1024];

    const char *dir = locale_dir;
    if (!dir || !*dir) {
        const char *env = g_getenv("DNSB_LOCALEDIR");
        if (env && *env && dir_has_messages(env)) {
            dir = env;
        }
    }
    /* AppImage: $APPDIR/usr/share/locale beats the compile-time install
       path, which would otherwise point at /usr/share/locale on the host. */
    if (!dir || !*dir) {
        const char *appdir = g_getenv("APPDIR");
        if (appdir && *appdir) {
            snprintf(appdir_locale, sizeof(appdir_locale),
                     "%s/usr/share/locale", appdir);
            if (dir_has_messages(appdir_locale)) dir = appdir_locale;
        }
    }
#ifdef DNSB_LOCALEDIR_BUILD
    if ((!dir || !*dir) && dir_has_messages(DNSB_LOCALEDIR_BUILD)) {
        dir = DNSB_LOCALEDIR_BUILD;
    }
#endif
#ifdef DNSB_LOCALEDIR_INSTALL
    if (!dir || !*dir) {
        dir = DNSB_LOCALEDIR_INSTALL;
    }
#endif
    if (!dir) dir = "/usr/share/locale";

    bindtextdomain(DNSB_GETTEXT_PACKAGE, dir);
    bind_textdomain_codeset(DNSB_GETTEXT_PACKAGE, "UTF-8");
    textdomain(DNSB_GETTEXT_PACKAGE);
}

static char *lang_pref_path(void) {
    const char *xdg = g_getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return g_build_filename(xdg, "dnsbenchmark", "lang", NULL);
    return g_build_filename(g_get_home_dir(), ".config", "dnsbenchmark", "lang", NULL);
}

char *dnsb_lang_pref_load(void) {
    char *path = lang_pref_path();
    gchar *contents = NULL;
    if (g_file_get_contents(path, &contents, NULL, NULL)) {
        g_strstrip(contents);
    }
    g_free(path);
    if (!contents || !*contents) {
        g_free(contents);
        return NULL;
    }
    return contents;
}

void dnsb_lang_pref_save(const char *code) {
    if (!code) code = "auto";
    char *path = lang_pref_path();
    char *dir = g_path_get_dirname(path);
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    g_file_set_contents(path, code, -1, NULL);
    g_free(path);
}

const char *dnsb_lang_code_to_language(const char *code) {
    if (!code) return NULL;
    if (strcmp(code, "auto") == 0) return NULL;
    if (strcmp(code, "en") == 0)   return "en_US:en";
    if (strcmp(code, "nb") == 0)   return "nb_NO:nb";
    return NULL;
}

void dnsb_lang_apply_pref_env(void) {
    char *pref = dnsb_lang_pref_load();
    if (!pref) return;
    const char *lang = dnsb_lang_code_to_language(pref);
    if (lang) g_setenv("LANGUAGE", lang, TRUE);
    g_free(pref);
}

/* glibc's gettext caches translations keyed by domain + locale. The internal
   counter `_nl_msg_cat_cntr` is checked on each gettext() call: bumping it
   forces the cache to reload, which is the documented (if internal) way to
   switch language at runtime without restarting the process. The same trick
   works with GNU gettext on MSYS2 / mingw. */
#if defined(__GLIBC__) || defined(__GNU_LIBRARY__)
extern int _nl_msg_cat_cntr;
#define DNSB_HAS_NL_CAT_CNTR 1
#endif

void dnsb_i18n_set_language(const char *code) {
    const char *lang = dnsb_lang_code_to_language(code);
    if (lang) g_setenv("LANGUAGE", lang, TRUE);
    else      g_unsetenv("LANGUAGE");

    setlocale(LC_ALL, "");

#ifdef DNSB_HAS_NL_CAT_CNTR
    ++_nl_msg_cat_cntr;
#endif
}
