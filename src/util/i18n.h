#ifndef DNSB_UTIL_I18N_H
#define DNSB_UTIL_I18N_H

#include <libintl.h>

#define DNSB_GETTEXT_PACKAGE "DNS-Benchmark"

/* Mark a string for runtime translation. */
#define _(s)  gettext(s)
/* Mark a string for extraction but defer translation (e.g. static tables). */
#define N_(s) (s)

/* Set up locale + textdomain. Pass NULL for `locale_dir` to fall back to the
   build/install defaults set via DNSB_LOCALEDIR_BUILD / _INSTALL. */
void dnsb_i18n_init(const char *locale_dir);

/* Read the persisted language preference. Returns one of "auto", "en", "nb"
   (caller frees). NULL means no preference recorded → treat as "auto". */
char *dnsb_lang_pref_load(void);

/* Write the language preference. code is "auto", "en", "nb", ... */
void  dnsb_lang_pref_save(const char *code);

/* Convert a short language code to a full POSIX locale string suitable for
   the LANGUAGE env var (e.g. "nb" → "nb_NO:nb"). Returns NULL for "auto". */
const char *dnsb_lang_code_to_language(const char *code);

/* Apply the persisted language preference to the LANGUAGE env var, if any.
   Must be called BEFORE dnsb_i18n_init() so setlocale picks it up. */
void dnsb_lang_apply_pref_env(void);

/* Switch the active gettext language at runtime. code = "auto", "en", "nb".
   Sets/clears LANGUAGE, re-applies setlocale, and invalidates the gettext
   translation cache so subsequent _() calls return the new language. */
void dnsb_i18n_set_language(const char *code);

#endif
