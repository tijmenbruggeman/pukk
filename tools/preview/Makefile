# Host preview: renders the real UI code to PNG files without hardware.
#   make -C tools/preview run     -> build-preview/*.png
# Needs managed_components/ (run an idf.py build once) and ImageMagick.

ROOT    := $(abspath ../..)
LVGL    := $(ROOT)/managed_components/lvgl__lvgl
OUT     := $(ROOT)/build-preview

LVGL_SRC := $(shell find $(LVGL)/src -name '*.c')
UI_SRC   := $(filter-out %/lvgl_port.c,$(wildcard $(ROOT)/components/ui/*.c $(ROOT)/components/ui/assets/*.c))
APP_SRC  := $(ROOT)/tools/preview/preview.c $(ROOT)/tools/preview/fake_packs.c \
            $(ROOT)/tools/preview/fake_battle.c $(ROOT)/components/packs/pack_format.c
SRC      := $(LVGL_SRC) $(UI_SRC) $(APP_SRC)
OBJ      := $(patsubst $(ROOT)/%.c,$(OUT)/obj/%.o,$(SRC))

CFLAGS  := -std=gnu11 -O1 -g -Wall -Wno-unused-parameter -DLV_CONF_INCLUDE_SIMPLE -DPUKK_PREVIEW \
           -I$(ROOT)/tools/preview -I$(ROOT)/tools/preview/stubs -I$(LVGL) \
           -I$(ROOT)/components/ui -I$(ROOT)/components/ui/include \
           -I$(ROOT)/components/packs/include -I$(ROOT)/components/ble/include \
           -I$(ROOT)/components/battle/include

$(OUT)/pukk-preview: $(OBJ)
	$(CC) $(OBJ) -o $@

$(OUT)/obj/%.o: $(ROOT)/%.c $(ROOT)/tools/preview/lv_conf.h
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@

run: $(OUT)/pukk-preview
	cd $(OUT) && ./pukk-preview
	cd $(OUT) && for f in *.ppm; do magick "$$f" "$${f%.ppm}.png" && rm "$$f"; done
	@ls $(OUT)/*.png

clean:
	rm -rf $(OUT)

.PHONY: run clean
