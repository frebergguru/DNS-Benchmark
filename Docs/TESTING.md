# Testing

```bash
ctest --test-dir build --output-on-failure       # everything (with network)
SKIP_NETWORK_TESTS=1 ctest --test-dir build      # offline only (CI)
```

CTest treats a `77` exit code as "skipped". The three network-touching
suites return 77 when `SKIP_NETWORK_TESTS` is set, so the same `ctest`
invocation works in CI without a working network.

## Suite at a glance

| Test                    | Type        | What it covers                                              |
|-------------------------|-------------|-------------------------------------------------------------|
| `test_dns_packet`       | Unit        | RFC 1035 encode/decode round-trip, EDNS0 DO bit, compressed name pointers, malformed-input rejection. |
| `test_stats`            | Unit        | Running mean/stddev/median against known vectors; empty-state behavior. |
| `test_resolvers_io`     | Unit        | TSV resolver list parsing including hostname column and comment lines. |
| `smoke_live`            | Integration | One UDP exchange against 1.1.1.1; validates the wire path end-to-end. |
| `smoke_engine`          | Integration | Multi-resolver benchmark across UDP + TCP + DoH + DoT against Cloudflare/Google/Quad9. |
| `smoke_transports`      | Integration | TCP / DoH / DoH-reuse / DoT / DoT-reuse exchanges against 1.1.1.1, prints RTTs. |

## Asserts and Release builds

CMake defaults to Release mode (which defines `NDEBUG`, which makes
`assert()` a no-op). The test targets opt out with `-UNDEBUG` (see
`tests/CMakeLists.txt`) so the assertions still execute. If you add a
new test, copy the `dnsb_add_test()` helper, **don't** call
`add_executable()` directly, or your asserts will be silently dropped.

## Smoke vs unit boundary

Anything that opens a socket, hits libcurl, or starts an OpenSSL
handshake is a smoke test. Anything that's pure CPU lives in `test_*`.
This boundary matters because:

- CI runs unit tests on every PR; smoke tests run on a separate matrix
  job that's allowed to fail (transient network issues shouldn't block
  merges).
- Offline development uses `SKIP_NETWORK_TESTS=1` so the live-network
  variability doesn't show up as flakes.

## Adding tests

Tests live in `tests/` and follow this template:

```c
/* tests/test_<thing>.c */
#include "<header-under-test>.h"
#include <assert.h>
#include <stdio.h>

static void test_basic(void) {
    /* arrange / act */
    assert(thing_under_test(42) == 42);
}

int main(void) {
    test_basic();
    printf("test_<thing>: OK\n");
    return 0;
}
```

Wire it up in `tests/CMakeLists.txt`:

```cmake
dnsb_add_test(test_<thing>)
```

That helper:

- Adds an executable from `<name>.c`
- Links against `dnsb_core`
- Strips `NDEBUG` so `assert()` runs
- Registers it with `ctest`

For a smoke test, also opt into the `77 = skip` convention:

```cmake
add_executable(smoke_<thing> smoke_<thing>.c)
target_link_libraries(smoke_<thing> PRIVATE dnsb_core)
target_compile_options(smoke_<thing> PRIVATE -UNDEBUG)
add_test(NAME smoke_<thing> COMMAND smoke_<thing>)
set_tests_properties(smoke_<thing> PROPERTIES
    SKIP_RETURN_CODE 77 TIMEOUT 30)
```

Inside the test, gate on the env var:

```c
if (getenv("SKIP_NETWORK_TESTS")) { puts("smoke_<thing>: SKIP"); return 77; }
```

## CI

`.github/workflows/build.yml` runs the matrix:

- `ubuntu-latest`: configure + build + offline tests (`SKIP_NETWORK_TESTS=1`)
  + live tests with `continue-on-error` to surface flakes without
  failing the job.
- `windows-latest` / MSYS2 mingw64: configure + build + offline tests only.
