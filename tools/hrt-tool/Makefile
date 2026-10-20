BUILD_DIR ?= build
PREFIX ?= /usr/local
CMAKE ?= cmake

.PHONY: all configure build test install update clean

all: build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=$(PREFIX)

build: configure
	$(CMAKE) --build $(BUILD_DIR) --parallel

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

install: build
	$(CMAKE) --install $(BUILD_DIR)

# Rebuild and reinstall without changing the cached install prefix.
update:
	@test -f $(BUILD_DIR)/CMakeCache.txt || { \
		echo "no build in $(BUILD_DIR); run 'make install' first"; exit 1; }
	$(CMAKE) --build $(BUILD_DIR) --parallel --target hrt-tool
	$(CMAKE) --install $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
