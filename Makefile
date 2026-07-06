BUILD_DIR ?= build-release
BUILD_TYPE ?= Release
CMAKE ?= cmake

SHELL := /bin/bash
MAKE_COLOR := \x1b[38;5;213m
BUILD_COLOR := \x1b[38;5;43m
CXX_COLOR := \x1b[38;5;189m
TARGET_COLOR := \x1b[38;5;117m
RESET_COLOR := \x1b[0m
COLOR_BUILD_OUTPUT = sed -u -E \
	-e 's/^(g?make\[[0-9]+\]: .*)$$/$(MAKE_COLOR)\1$(RESET_COLOR)/' \
	-e 's/^(-- .*)$$/$(CXX_COLOR)\1$(RESET_COLOR)/' \
	-e 's/^(Build .*)$$/$(BUILD_COLOR)\1$(RESET_COLOR)/' \
	-e 's/^(\[100%\])( Built target .*)$$/$(BUILD_COLOR)\1$(RESET_COLOR)$(TARGET_COLOR)\2$(RESET_COLOR)/' \
	-e 's/^(\[[ 0-9]+%\].*)$$/$(BUILD_COLOR)\1$(RESET_COLOR)/'
COLOR_MAKE_OUTPUT = sed -u -E 's/\x1b\[[0-9;]*m//g' | sed -u -E \
	-e 's/^(g?make\[[0-9]+\]: .*)$$/$(MAKE_COLOR)\1$(RESET_COLOR)/' \
	-e 's/^(-- .*)$$/$(CXX_COLOR)\1$(RESET_COLOR)/' \
	-e 's/^(Build .*)$$/$(BUILD_COLOR)\1$(RESET_COLOR)/' \
	-e 's/^(\[100%\])( Built target .*)$$/$(BUILD_COLOR)\1$(RESET_COLOR)$(TARGET_COLOR)\2$(RESET_COLOR)/' \
	-e 's/^(\[[ 0-9]+%\].*)$$/$(BUILD_COLOR)\1$(RESET_COLOR)/'

ifdef DESTDIR
INSTALL_COMMAND = DESTDIR="$(DESTDIR)" $(CMAKE) --install $(BUILD_DIR)
else
INSTALL_COMMAND = $(CMAKE) --install $(BUILD_DIR)
endif

.PHONY: all configure configure-tests build test install

all: build

configure:
	@echo '$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DMGREP_BUILD_TESTS=OFF'
	@set -o pipefail; $(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DMGREP_BUILD_TESTS=OFF 2>&1 | $(COLOR_BUILD_OUTPUT)

configure-tests:
	@echo '$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DMGREP_BUILD_TESTS=ON'
	@set -o pipefail; $(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DMGREP_BUILD_TESTS=ON 2>&1 | $(COLOR_BUILD_OUTPUT)

build: configure
	@set -o pipefail; $(CMAKE) --build $(BUILD_DIR) 2>&1 | $(COLOR_MAKE_OUTPUT)

test: configure-tests
	@set -o pipefail; $(CMAKE) --build $(BUILD_DIR) 2>&1 | $(COLOR_BUILD_OUTPUT)
	cd $(BUILD_DIR) && ./tests

install:
	@set -o pipefail; if [ -n "$$SUDO_USER" ] && [ "$$SUDO_USER" != "root" ]; then \
		sudo -u "$$SUDO_USER" $(MAKE) BUILD_DIR="$(BUILD_DIR)" BUILD_TYPE="$(BUILD_TYPE)" CMAKE="$(CMAKE)" build 2>&1 | $(COLOR_BUILD_OUTPUT); \
	else \
		$(MAKE) BUILD_DIR="$(BUILD_DIR)" BUILD_TYPE="$(BUILD_TYPE)" CMAKE="$(CMAKE)" build 2>&1 | $(COLOR_BUILD_OUTPUT); \
	fi
	@echo '$(INSTALL_COMMAND)'
	@set -o pipefail; $(INSTALL_COMMAND) 2>&1 | $(COLOR_BUILD_OUTPUT)
