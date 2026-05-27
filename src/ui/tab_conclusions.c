#include "tab_conclusions.h"

#include "../util/i18n.h"

dnsb_tab_conclusions dnsb_tab_conclusions_new(void) {
    dnsb_tab_conclusions t = {0};
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    /* GtkLabel with Pango markup so we can bold the winners, indent
       bullet rows, and use color accents — GtkTextView is plain text only. */
    GtkWidget *label = gtk_label_new(NULL);
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_yalign(GTK_LABEL(label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_selectable(GTK_LABEL(label), FALSE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 80);
    gtk_widget_set_margin_top(label, 18);
    gtk_widget_set_margin_bottom(label, 18);
    gtk_widget_set_margin_start(label, 24);
    gtk_widget_set_margin_end(label, 24);

    gtk_label_set_markup(GTK_LABEL(label), _(
        "<i>Conclusions will appear here after a benchmark run completes.</i>\n\n"
        "Press <b>Run</b> in the toolbar (or <b>Ctrl+R</b>) to start."));

    gtk_container_add(GTK_CONTAINER(scroll), label);

    t.root = scroll;
    t.text_view = label;
    t.buffer = NULL;
    gtk_widget_show_all(scroll);
    return t;
}

void dnsb_tab_conclusions_set_text(dnsb_tab_conclusions *t, const char *markup) {
    if (!t || !t->text_view) return;
    gtk_label_set_markup(GTK_LABEL(t->text_view), markup);
}
