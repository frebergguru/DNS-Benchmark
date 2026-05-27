#include "tab_conclusions.h"

#include "../util/i18n.h"

dnsb_tab_conclusions dnsb_tab_conclusions_new(void) {
    dnsb_tab_conclusions t = {0};
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget *view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 16);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 16);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 16);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), 16);

    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_set_text(buf, _(
        "Conclusions will appear here after a benchmark run completes.\n"), -1);

    gtk_container_add(GTK_CONTAINER(scroll), view);

    t.root = scroll;
    t.text_view = view;
    t.buffer = buf;
    gtk_widget_show_all(scroll);
    return t;
}

void dnsb_tab_conclusions_set_text(dnsb_tab_conclusions *t, const char *text) {
    if (!t || !t->buffer) return;
    gtk_text_buffer_set_text(t->buffer, text, -1);
}
