#ifndef DNSB_UI_TAB_CONCLUSIONS_H
#define DNSB_UI_TAB_CONCLUSIONS_H

#include <gtk/gtk.h>

typedef struct {
    GtkWidget *root;
    GtkWidget *text_view;
    GtkTextBuffer *buffer;
} dnsb_tab_conclusions;

dnsb_tab_conclusions dnsb_tab_conclusions_new(void);
void dnsb_tab_conclusions_set_text(dnsb_tab_conclusions *t, const char *text);

#endif
