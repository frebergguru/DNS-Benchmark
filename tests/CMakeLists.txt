function(dnsb_add_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE dnsb_core)
    # Force assertions to be active even in Release builds.
    target_compile_options(${name} PRIVATE -UNDEBUG)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

dnsb_add_test(test_dns_packet)
dnsb_add_test(test_stats)
dnsb_add_test(test_resolvers_io)

# Live network smoke check — skipped via SKIP_NETWORK_TESTS env var.
add_executable(smoke_live smoke_live.c)
target_link_libraries(smoke_live PRIVATE dnsb_core)
target_compile_options(smoke_live PRIVATE -UNDEBUG)
add_test(NAME smoke_live COMMAND smoke_live)
set_tests_properties(smoke_live PROPERTIES SKIP_RETURN_CODE 77)

add_executable(smoke_engine smoke_engine.c)
target_link_libraries(smoke_engine PRIVATE dnsb_core)
target_compile_options(smoke_engine PRIVATE -UNDEBUG)
add_test(NAME smoke_engine COMMAND smoke_engine)
set_tests_properties(smoke_engine PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)

add_executable(smoke_transports smoke_transports.c)
target_link_libraries(smoke_transports PRIVATE dnsb_core)
target_compile_options(smoke_transports PRIVATE -UNDEBUG)
add_test(NAME smoke_transports COMMAND smoke_transports)
set_tests_properties(smoke_transports PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)
