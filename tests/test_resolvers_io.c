#include "engine/engine.h"
#include "io/resolvers_io.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *fixture =
    "# header\n"
    "Cloudflare DNS\tCloudflare\t1.1.1.1\tudp\t53\n"
    "Google v6\tGoogle\t2001:4860:4860::8888\tudp\t53\n"
    "Quad9 DoT\tQuad9\t9.9.9.9\tdot\t853\n"
    "bad-line-no-transport-or-addr\n"
    "\n";

int main(void) {
    char path[] = "/tmp/dnsb_test_resolvers_XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *f = fdopen(fd, "w");
    assert(f);
    fputs(fixture, f);
    fclose(f);

    dnsb_engine *e = dnsb_engine_new();
    int n = dnsb_load_resolvers_tsv(path, e);
    assert(n == 3);
    assert(dnsb_engine_resolver_count(e) == 3);

    dnsb_resolver *r0 = dnsb_engine_resolver_at(e, 0);
    assert(strcmp(r0->name, "Cloudflare DNS") == 0);
    assert(strcmp(r0->addr, "1.1.1.1") == 0);
    assert(r0->transport == DNSB_TRANSPORT_UDP);
    assert(r0->port == 53);

    dnsb_resolver *r2 = dnsb_engine_resolver_at(e, 2);
    assert(r2->transport == DNSB_TRANSPORT_DOT);
    assert(r2->port == 853);

    dnsb_engine_free(e);
    remove(path);
    printf("test_resolvers_io: OK\n");
    return 0;
}
