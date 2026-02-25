BUILD_DIR   := build
TARGET      := $(BUILD_DIR)/v4l2_camera
TOOLCHAIN   := toolchain-imx6ull.cmake
BOARD_DIR   := /home/swaggywong7

.PHONY: all build configure push camera clean

all: build

configure:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. \
		-DCMAKE_TOOLCHAIN_FILE=../$(TOOLCHAIN) \
		-DBUILD_TESTS=OFF \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON

build: configure
	@$(MAKE) -C $(BUILD_DIR) -j$(shell nproc)
	@echo ">>> Build OK: $(TARGET)"

push: build
	@adb devices | grep -v "List of" | grep device > /dev/null || \
		(echo "ERROR: no adb device connected" && exit 1)
	@adb shell "mkdir -p $(BOARD_DIR)"
	@adb push $(TARGET) $(BOARD_DIR)/v4l2_camera
	@adb shell "chmod +x $(BOARD_DIR)/v4l2_camera"
	@echo ">>> Pushed to $(BOARD_DIR)/v4l2_camera"

camera:
	@adb devices | grep -v "List of" | grep device > /dev/null || \
		(echo "ERROR: no adb device connected" && exit 1)
	@echo "=== Video devices ==="
	@adb shell "ls -la /dev/video* 2>/dev/null || echo 'no /dev/video* found'"
	@echo "=== V4L2 capabilities ==="
	@adb shell "v4l2-ctl --list-devices 2>/dev/null || echo 'v4l2-ctl not available'"

clean:
	@rm -rf $(BUILD_DIR)
	@echo ">>> Cleaned"
