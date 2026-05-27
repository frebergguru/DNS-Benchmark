#include "tab_help.h"

#include "../util/i18n.h"

GtkWidget *dnsb_tab_help_new(void) {
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget *label = gtk_label_new(NULL);
    gtk_label_set_use_markup(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_yalign(GTK_LABEL(label), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 90);
    gtk_widget_set_margin_top(label, 18);
    gtk_widget_set_margin_bottom(label, 18);
    gtk_widget_set_margin_start(label, 24);
    gtk_widget_set_margin_end(label, 24);

    gtk_label_set_markup(GTK_LABEL(label), _(
        "<span size='x-large' weight='bold'>Help</span>\n"
        "\n"
        "<b>Keyboard shortcuts</b>\n"
        "  <tt>Ctrl+R</tt>    Run benchmark\n"
        "  <tt>Ctrl+.</tt>    Stop benchmark\n"
        "  <tt>Ctrl+N</tt>    Add custom resolver\n"
        "  <tt>Ctrl+S</tt>    Save results to CSV\n"
        "  <tt>Ctrl+,</tt>    Open Settings\n"
        "  <tt>F1</tt>         Open this Help tab\n"
        "  <tt>Alt+F4</tt>     Close the window\n"
        "\n"
        "<b>What each bar means</b>\n"
        "  <span foreground='#cc3333'>■</span>  <b>Cached</b> — repeat query for a popular domain. The resolver "
        "answers from its own cache. This is the case that dominates everyday browsing.\n"
        "  <span foreground='#22aa44'>■</span>  <b>Uncached</b> — fresh random subdomain. The resolver has to do "
        "a recursive walk all the way to the authoritative server.\n"
        "  <span foreground='#7755bb'>■</span>  <b>.com</b> — fresh random <tt>&lt;rand&gt;.com</tt>. Isolates the "
        "round-trip to the <tt>.com</tt> TLD servers.\n"
        "\n"
        "<b>Status dots</b>\n"
        "  <span foreground='#22aa44'>●</span> green   healthy, responding normally\n"
        "  <span foreground='#cc3333'>●</span> red     unresponsive or refusing queries\n"
        "  <span foreground='#cc7722'>●</span> orange  redirector — hijacks invalid names into A records\n"
        "  <span foreground='#888888'>●</span> grey    sidelined after too many failures\n"
        "  <span foreground='#cc8800'>●</span> filled  currently configured on your system\n"
        "  <span foreground='#cc8800'>○</span> hollow  available but not your current resolver\n"
        "  A yellow ring marks a resolver you've pinned.\n"
        "\n"
        "<b>Glossary</b>\n"
        "  <b>Resolver</b>    A DNS server you can ask names of. Examples: 1.1.1.1, 8.8.8.8.\n"
        "  <b>Sidelined</b>   Stopped probing because 10 consecutive queries failed.\n"
        "  <b>Redirector</b> A resolver that returns a fake A record (typically an ad page) "
        "for names that should be NXDOMAIN.\n"
        "  <b>DNSSEC</b>     Cryptographic validation of DNS answers. The AD bit in a reply "
        "indicates the resolver verified signatures.\n"
        "  <b>DoH / DoT</b>  DNS over HTTPS / DNS over TLS — encrypted transports.\n"
        "  <b>ETA</b>        Estimated time remaining, shown after the first ~3 seconds and "
        "5% progress so the early-stage extrapolation doesn't lie.\n"
        "\n"
        "<b>Tips</b>\n"
        "  • Close bandwidth-heavy apps before running — other traffic on your link inflates "
        "the timings.\n"
        "  • Run several times across the day. One run is a snapshot, not a verdict.\n"
        "  • Pin your current resolver (right-click in <b>Tabular Data</b>) so it stays at "
        "the top no matter how you sort.\n"
        "  • The <b>Clone</b> button takes a frozen snapshot you can compare against later "
        "runs.\n"
        "\n"
        "<b>Files this app writes</b>\n"
        "  <tt>~/.config/dnsbenchmark/theme</tt>   last-chosen theme (auto / dark / light)\n"
        "  <tt>~/.config/dnsbenchmark/lang</tt>    last-chosen language (auto / en / nb)\n"
        "\n"
        "<i>Project source and updates: github.com/frebergguru/DNS-Benchmark</i>"));

    gtk_container_add(GTK_CONTAINER(scroll), label);
    gtk_widget_show_all(scroll);
    return scroll;
}
