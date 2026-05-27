function(dnsb_add_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE dnsb_core)
    # Force assertions to be active even in Release builds.
    target_compile_options(${name} PRIVATE -UNDEBUG)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Smoke variant: same compile flags, but the test reports 77 to CTest when
# SKIP_NETWORK_TESTS is set, and we forward that env var explicitly so the
# child process sees it even if the CI runner scrubs the environment.
function(dnsb_add_smoke_test name)
    dnsb_add_test(${name})
    set_tests_properties(${name} PROPERTIES
        SKIP_RETURN_CODE 77
        ENVIRONMENT "SKIP_NETWORK_TESTS=$ENV{SKIP_NETWORK_TESTS}")
endfunction()

dnsb_add_test(test_dns_packet)
dnsb_add_test(test_stats)
dnsb_add_test(test_resolvers_io)

dnsb_add_smoke_test(smoke_live)
dnsb_add_smoke_test(smoke_engine)
set_tests_properties(smoke_engine PROPERTIES TIMEOUT 30)
dnsb_add_smoke_test(smoke_transports)
set_tests_properties(smoke_transports PROPERTIES TIMEOUT 30)
