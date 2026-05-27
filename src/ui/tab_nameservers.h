#ifndef DNSB_UI_TAB_NAMESERVERS_H
#define DNSB_UI_TAB_NAMESERVERS_H

#include <gtk/gtk.h>
#include "chart.h"

typedef struct {
    GtkWidget   *root;
    dnsb_chart  *chart;
    GtkWidget   *summary_label;
} dnsb_tab_nameservers;

dnsb_tab_nameservers dnsb_tab_nameservers_new(GtkTreeModel *model);
void dnsb_tab_nameservers_set_summary(dnsb_tab_nameservers *t, const char *markup);

#endif
