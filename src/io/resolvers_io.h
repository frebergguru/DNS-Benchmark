#ifndef DNSB_IO_RESOLVERS_IO_H
#define DNSB_IO_RESOLVERS_IO_H

#include "../engine/engine.h"

/* Load a tab-separated resolver list and append each entry to the engine.
   Format per line: name<TAB>owner<TAB>addr<TAB>transport<TAB>port
   transport is one of: udp tcp doh dot
   Lines starting with '#' or empty are skipped.
   Returns the number of resolvers loaded, or -1 on failure. */
int dnsb_load_resolvers_tsv(const char *path, dnsb_engine *eng);

/* Load test-domain lines (one per line, # comments). Returns count, or -1. */
int dnsb_load_domains_txt(const char *path, char ***out_lines, size_t *out_n);

#endif
