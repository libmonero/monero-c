# Variants of the build of monero-c, with the targets that the Makefile of monero-project has. Each target calls bin/build_libmonero_c.sh, which
# builds monero-project and monero-cpp first and monero-c in its own directory. The first build takes hours, and the next ones only what changed.
#
#   make release                       the library, in build/
#   make release-test                  with the tests, and runs the unit tests
#   make coverage                      with gcov, in build-coverage/, then make coverage-report
#   make sanitizers                    with ASan and UBSan, in build-sanitizers/, and runs the unit tests
#   make JOBS=2 CMAKE_ARGS=-DUSE_DEVICE_TREZOR=OFF release
SHELL := /bin/bash
.DEFAULT_GOAL := help

JOBS ?= 1
CMAKE_ARGS ?=

# the unit tests need no node
UNIT_TESTS := utils daemon wallet wallet_keys wallet_offline wallet_multisig rpc_connection abi arguments fuzz threads cpp
TEST_DIR ?= build
DAEMON_URI ?= http://127.0.0.1:18081
COVERAGE_TITLE ?= monero-c coverage

SANITIZE_LINK := -fsanitize=address,undefined
SANITIZE_FLAGS := $(SANITIZE_LINK) -fno-omit-frame-pointer -fno-sanitize-recover=undefined
SANITIZE_ENV := ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 LSAN_OPTIONS=suppressions=tests/c/sanitizers/lsan.supp \
                UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1

# $(1) build directory, $(2) cmake arguments
define build
	BUILD_DIR=$(1) JOBS=$(JOBS) ./bin/build_libmonero_c.sh $(2) $(CMAKE_ARGS)
endef

.PHONY: help all release release-test release-static debug debug-test coverage coverage-report sanitizers unit-test integration-test clean

help:
	@grep -E '^[a-z-]+:.*?## .*$$' $(MAKEFILE_LIST) | awk 'BEGIN {FS = ":.*?## "}; {printf "%-18s %s\n", $$1, $$2}'

all: release ## The same as release

release: ## Build the library in build/
	$(call build,build,-DCMAKE_BUILD_TYPE=Release)

release-test: ## Build the library and the tests in build/, and run the unit tests
	$(call build,build,-DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON)
	$(MAKE) unit-test TEST_DIR=build

release-static: ## Build the library with its dependencies static, in build/
	$(call build,build,-DCMAKE_BUILD_TYPE=Release -DMONERO_C_STATIC_DEPENDENCIES=ON)

debug: ## Build the library for debugging in build-debug/
	$(call build,build-debug,-DCMAKE_BUILD_TYPE=Debug)

debug-test: ## Build the library and the tests for debugging in build-debug/, and run the unit tests
	$(call build,build-debug,-DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON)
	$(MAKE) unit-test TEST_DIR=build-debug

coverage: ## Build with gcov in build-coverage/ and run the unit tests, then make coverage-report
	$(call build,build-coverage,-DBUILD_TESTS=ON -DENABLE_COVERAGE=ON)
	$(MAKE) unit-test TEST_DIR=build-coverage

coverage-report: ## Make build-coverage/coverage_html from the data of the tests that ran, needs lcov
	cd build-coverage && lcov --capture --directory . --ignore-errors gcov,source --output-file coverage_full.info
	cd build-coverage && lcov --extract coverage_full.info "$(CURDIR)/src/*" --output-file coverage.info
	cd build-coverage && genhtml coverage.info --output-directory coverage_html --title "$(COVERAGE_TITLE)"
	cd build-coverage && lcov --summary coverage.info

sanitizers: ## Build with ASan and UBSan in build-sanitizers/ and run the unit tests
	$(call build,build-sanitizers,-DBUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_C_FLAGS=$(SANITIZE_FLAGS)" "-DCMAKE_CXX_FLAGS=$(SANITIZE_FLAGS)" \
		"-DCMAKE_EXE_LINKER_FLAGS=$(SANITIZE_LINK)" "-DCMAKE_SHARED_LINKER_FLAGS=$(SANITIZE_LINK)")
	$(MAKE) unit-test TEST_DIR=build-sanitizers UNIT_ENV='$(SANITIZE_ENV)'

unit-test: ## Run the unit tests of TEST_DIR, build by default
	for test in $(UNIT_TESTS); do $(UNIT_ENV) $(TEST_DIR)/tests/c/unit/monero_c_$${test}_unit_tests || exit 1; done

integration-test: ## Run the integration tests of TEST_DIR against a regtest node at DAEMON_URI
	for test in daemon wallet; do MONERO_C_TEST_DAEMON_URI=$(DAEMON_URI) $(TEST_DIR)/tests/c/integration/monero_c_$${test}_integration_tests || exit 1; done

clean: ## Remove the build directories of monero-c, not the ones of monero-project and monero-cpp
	rm -rf build build-debug build-coverage build-sanitizers
