#ifndef DNSB_IO_CSV_EXPORT_H
#define DNSB_IO_CSV_EXPORT_H

#include "../engine/engine.h"

/* Write a CSV summary of the engine's current results. Returns 0 on success. */
int dnsb_csv_export(const char *path, dnsb_engine *eng);

#endif
