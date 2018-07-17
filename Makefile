PROJECT_NAME := esp32-web-music-player


EXTRA_COMPONENT_DIRS := $(abspath drv)	$(abspath lvgl) #$(abspath lv_examples)

include $(IDF_PATH)/make/project.mk
