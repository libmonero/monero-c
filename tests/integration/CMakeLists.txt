add_executable(monero_c_daemon_integration_tests monero_c_daemon_integration_tests.c)
target_link_libraries(monero_c_daemon_integration_tests PRIVATE monero_c monero_c_test_common)

# needs a regtest node (see docker-compose.yml in this folder). Without one, it is reported as skipped.
add_test(NAME monero_c_daemon_integration_tests COMMAND monero_c_daemon_integration_tests)
set_tests_properties(monero_c_daemon_integration_tests PROPERTIES SKIP_RETURN_CODE 77)

# needs a regtest node and a monero-wallet-rpc server (see docker-compose.yml in this folder). Each part without its
# URI is reported as skipped, and the test is skipped when both are missing.
add_executable(monero_c_wallet_integration_tests monero_c_wallet_integration_tests.c)
target_link_libraries(monero_c_wallet_integration_tests PRIVATE monero_c monero_c_test_common)
add_test(NAME monero_c_wallet_integration_tests COMMAND monero_c_wallet_integration_tests)
set_tests_properties(monero_c_wallet_integration_tests PROPERTIES SKIP_RETURN_CODE 77)
