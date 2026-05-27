#ifndef DNSB_UI_CHART_H
#define DNSB_UI_CHART_H

#include <gtk/gtk.h>

typedef struct dnsb_chart dnsb_chart;

dnsb_chart *dnsb_chart_new(GtkTreeModel *model);
GtkWidget  *dnsb_chart_widget(dnsb_chart *c);
void        dnsb_chart_redraw(dnsb_chart *c);
void        dnsb_chart_lock_scale(dnsb_chart *c, double max_ms_or_zero);

/* Render the chart for the bound model to a PNG file. */
int         dnsb_chart_save_png(dnsb_chart *c, const char *path, int width);

#endif
