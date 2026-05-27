#ifndef DNSB_UI_WINDOW_H
#define DNSB_UI_WINDOW_H

#include <gtk/gtk.h>

#include "../engine/engine.h"

typedef struct dnsb_window dnsb_window;

dnsb_window *dnsb_window_new(GtkApplication *app, dnsb_engine *engine, const char *data_dir);
void         dnsb_window_present(dnsb_window *w);
void         dnsb_window_destroy(dnsb_window *w);

/* Marks resolvers whose address matches one of the given strings as
   system-configured (filled dot in the status column). */
void         dnsb_window_mark_system_resolvers(dnsb_window *w, char **sys, size_t n);

#endif
