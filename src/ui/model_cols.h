#ifndef DNSB_UI_MODEL_COLS_H
#define DNSB_UI_MODEL_COLS_H

/* Column ids for the shared resolver list store. */
enum {
    DNSB_COL_NAME = 0,        /* G_TYPE_STRING */
    DNSB_COL_OWNER,           /* G_TYPE_STRING */
    DNSB_COL_ADDRESS,         /* G_TYPE_STRING */
    DNSB_COL_TRANSPORT,       /* G_TYPE_STRING */
    DNSB_COL_STATUS,          /* G_TYPE_INT  -1=sidelined 0=idle 1=ok 2=fail 3=redirect */
    DNSB_COL_SYSTEM,          /* G_TYPE_BOOLEAN */
    DNSB_COL_CACHED_MS,       /* G_TYPE_DOUBLE */
    DNSB_COL_UNCACHED_MS,     /* G_TYPE_DOUBLE */
    DNSB_COL_DOTCOM_MS,       /* G_TYPE_DOUBLE */
    DNSB_COL_RELIABILITY,     /* G_TYPE_DOUBLE percent */
    DNSB_COL_QUERIES_SENT,    /* G_TYPE_INT */
    DNSB_COL_QUERIES_OK,      /* G_TYPE_INT */
    DNSB_COL_REDIRECTS,       /* G_TYPE_BOOLEAN */
    DNSB_COL_DNSSEC,          /* G_TYPE_BOOLEAN */
    DNSB_COL_PINNED,          /* G_TYPE_BOOLEAN */
    DNSB_COL_ENGINE_INDEX,    /* G_TYPE_INT (lookup into engine resolver array) */
    DNSB_N_COLS
};

#endif
