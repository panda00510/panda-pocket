# Host-side checks and mock demo: no ESP-IDF, just cc + git (fetches a pinned cJSON).
CJSON_TAG := v1.7.18
B := build
INC := -I../components/pp_wifi -I../components/pp_bus -I../components/pp_nav -I../components/pp_storage -I$(B)/cJSON
SRC := ../components/pp_bus/bus_parse.c ../components/pp_bus/bus_mock.c \
       ../components/pp_nav/nav_parse.c ../components/pp_nav/nav_mock.c \
       ../components/pp_storage/favs.c ../components/pp_wifi/ap_list.c $(B)/cJSON/cJSON.c
CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE

$(B)/cJSON/cJSON.c:
	mkdir -p $(B) && git clone -q --depth 1 --branch $(CJSON_TAG) https://github.com/DaveGamble/cJSON $(B)/cJSON

$(B)/test_logic: tests/test_logic.c $(SRC)
	$(CC) $(CFLAGS) $(INC) $^ -lm -o $@

$(B)/demo: demo.c $(SRC)
	$(CC) $(CFLAGS) $(INC) $^ -lm -o $@

test: $(B)/test_logic
	$(B)/test_logic
demo: $(B)/demo
	$(B)/demo
clean:
	rm -rf $(B)
.PHONY: test demo clean

# Desktop UI preview (needs cmake, ninja, SDL2: brew install cmake ninja sdl2). Uses the real components/pp_ui/ui.c.
$(B)/lvgl:
	mkdir -p $(B) && git clone -q --depth 1 --branch v9.5.0 https://github.com/lvgl/lvgl $(B)/lvgl
sim: $(B)/lvgl $(B)/cJSON/cJSON.c
	cmake -S sim -B $(B)/sim -G Ninja -DCMAKE_BUILD_TYPE=Release && ninja -C $(B)/sim
sim-run: sim
	$(B)/sim/sim
# Offscreen screenshots of every screen into host/build/shot_*.bmp
sim-shots: sim
	SDL_VIDEODRIVER=dummy $(B)/sim/sim --shot $(B)/shot_home.bmp
	for s in menu bus nav settings; do SDL_VIDEODRIVER=dummy $(B)/sim/sim --shot $(B)/shot_$$s.bmp --screen $$s; done
.PHONY: sim sim-run sim-shots

# Regression guard: ASan/UBSan build of the simulator, every screen in both languages. Fails on any crash/overflow.
sim-check: $(B)/lvgl $(B)/cJSON/cJSON.c
	cmake -S sim -B $(B)/sim_asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
	    -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -g" >/dev/null && ninja -C $(B)/sim_asan
	set -e; for l in en zh; do for s in menu bus numpad:831 arrivals nav places route settings wifi password; do \
	    SDL_VIDEODRIVER=dummy PP_SIM_FAVS=83139 $(B)/sim_asan/sim --shot $(B)/check.bmp --lang $$l --screen $$s >/dev/null; \
	done; done; echo "sim-check ok"
.PHONY: sim-check
