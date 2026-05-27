#include "tab_nameservers.h"

#include "../util/i18n.h"

dnsb_tab_nameservers dnsb_tab_nameservers_new(GtkTreeModel *model) {
    dnsb_tab_nameservers t = {0};

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *summary = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(summary), 0.0);
    gtk_widget_set_margin_top(summary, 8);
    gtk_widget_set_margin_bottom(summary, 8);
    gtk_widget_set_margin_start(summary, 12);
    gtk_widget_set_margin_end(summary, 12);
    gtk_label_set_markup(GTK_LABEL(summary), _(
        "<i>Press <b>Run</b> in the toolbar to start benchmarking.</i>"));
    t.summary_label = summary;

    gtk_box_pack_start(GTK_BOX(box), summary, FALSE, FALSE, 0);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(box), sep, FALSE, FALSE, 0);

    t.chart = dnsb_chart_new(model);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), dnsb_chart_widget(t.chart));
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    /* Legend strip. */
    GtkWidget *legend = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(legend), 0.5);
    gtk_label_set_markup(GTK_LABEL(legend), _(
        "<span foreground='#cc3333'>■</span> cached  "
        "<span foreground='#22aa44'>■</span> uncached  "
        "<span foreground='#7755bb'>■</span> .com  "
        "  <span foreground='#999999'>●</span> filled = system-configured, hollow = available"));
    gtk_widget_set_margin_top(legend, 6);
    gtk_widget_set_margin_bottom(legend, 8);
    gtk_box_pack_start(GTK_BOX(box), legend, FALSE, FALSE, 0);

    t.root = box;
    gtk_widget_show_all(box);
    return t;
}

void dnsb_tab_nameservers_set_summary(dnsb_tab_nameservers *t, const char *markup) {
    if (!t || !t->summary_label) return;
    gtk_label_set_markup(GTK_LABEL(t->summary_label), markup);
}
