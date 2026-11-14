# Makefile for Sigma UCI Chess Engine
# This is a wrapper around CMake for convenience

BUILD_DIR = build
TARGET = $(BUILD_DIR)/sigma-uci

# Default target
all: $(TARGET)

# Configure and build with CMake. FORCE makes this run every time so CMake decides what is
# out of date; without it make sees the existing binary and never rebuilds.
$(TARGET): FORCE
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. > /dev/null
	@$(MAKE) -C $(BUILD_DIR) --no-print-directory

# Clean build artifacts
clean:
	@rm -rf $(BUILD_DIR)
	@echo "Clean complete"

# Run the engine
run: $(TARGET)
	@$(TARGET)

# Install target
install: $(TARGET)
	@install -m 755 $(TARGET) /usr/local/bin/sigma-uci
	@echo "Installed to /usr/local/bin/sigma-uci"

# Uninstall target
uninstall:
	@rm -f /usr/local/bin/sigma-uci
	@echo "Uninstalled from /usr/local/bin"

# Reconfigure CMake (if CMakeLists.txt changed)
reconfigure:
	@rm -rf $(BUILD_DIR)
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake ..

# Source files for clang-format. .f files are forward-decl headers (plain C).
# Vendored Fathom (src/Syzygy/fathom) keeps its upstream formatting.
FORMAT_FILES := $(shell find src -path src/Syzygy/fathom -prune -o -type f \( -name '*.c' -o -name '*.h' -o -name '*.f' \) -print)

# Apply clang-format in place.
format:
	@clang-format -i $(FORMAT_FILES)
	@echo "Formatted $(words $(FORMAT_FILES)) files."

# Verify formatting without modifying. Exits non-zero if anything would change.
format-check:
	@clang-format --dry-run --Werror $(FORMAT_FILES)
	@echo "Format check passed."

# Show help
help:
	@echo "Sigma UCI Chess Engine - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build the engine (default)"
	@echo "  clean        - Remove build directory"
	@echo "  run          - Build and run the engine"
	@echo "  install      - Install to /usr/local/bin"
	@echo "  uninstall    - Remove from /usr/local/bin"
	@echo "  reconfigure  - Reconfigure CMake"
	@echo "  format       - Apply clang-format to all C/.h/.f sources"
	@echo "  format-check - Verify formatting (CI-friendly, non-modifying)"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Build system: CMake (unity build enabled)"
	@echo "Build directory: $(BUILD_DIR)/"

.PHONY: all clean run install uninstall reconfigure format format-check help FORCE
FORCE:
