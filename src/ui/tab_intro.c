#include "tab_intro.h"

#include "../util/i18n.h"

GtkWidget *dnsb_tab_intro_new(void) {
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget *label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_yalign(GTK_LABEL(label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 80);
    gtk_label_set_selectable(GTK_LABEL(label), TRUE);
    gtk_widget_set_margin_top(label, 16);
    gtk_widget_set_margin_bottom(label, 16);
    gtk_widget_set_margin_start(label, 24);
    gtk_widget_set_margin_end(label, 24);

    gtk_label_set_markup(GTK_LABEL(label), _(
        "<span size='x-large' weight='bold'>DNS Benchmark</span>\n\n"
        "This tool measures the performance and reliability of DNS resolvers "
        "by timing <b>cached</b>, <b>uncached</b>, and <b>.com</b> lookups against "
        "each server.\n\n"
        "<b>Before you start</b>\n"
        "  • Close bandwidth-heavy apps. Other traffic on your link will "
        "inflate response times and skew results.\n"
        "  • Run the benchmark several times across the day; a single run is a "
        "snapshot, not a verdict.\n"
        "  • The first run does no real work — it just primes each resolver’s "
        "cache. The reported cached numbers come from subsequent queries.\n\n"
        "<b>How to read the results</b>\n"
        "  • <span foreground='#cc3333'>Red</span> bar = cached lookup (the most common case).\n"
        "  • <span foreground='#22aa44'>Green</span> bar = uncached lookup (recursive walk).\n"
        "  • <span foreground='#7755bb'>Purple</span> bar = .com TLD lookup.\n"
        "  • <b>Status dots:</b> filled = currently configured on this machine; "
        "green = healthy; red = unresponsive; orange = redirects bad domains to ads.\n\n"
        "<b>Tabs</b>\n"
        "  • <b>Nameservers</b> — the main view: live bar chart + status list.\n"
        "  • <b>Tabular Data</b> — every measurement in a sortable table.\n"
        "  • <b>Conclusions</b> — ranked recommendation after the run.\n\n"
        "Use the toolbar above to start, stop, save results, or add custom "
        "resolvers."));

    gtk_container_add(GTK_CONTAINER(scroll), label);
    gtk_widget_show_all(scroll);
    return scroll;
}
