#ifndef DNSB_UI_TAB_TABULAR_H
#define DNSB_UI_TAB_TABULAR_H

#include <gtk/gtk.h>

/* Builds a sortable table view bound to the provided model. */
GtkWidget *dnsb_tab_tabular_new(GtkTreeModel *model);

/* Pin-first comparator (pinned rows always rank first, then column-typed compare). */
int dnsb_pin_first_compare(GtkTreeModel *m, GtkTreeIter *a, GtkTreeIter *b, gpointer data);

#endif
