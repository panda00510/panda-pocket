# Panda Pocket

ESP-IDF + LVGL 固件，目标板 M5Stack StopWatch（C152）。进度与阻塞见 `PROGRESS.md`，用法见 `README.md`。

## 结构

- `components/hal_board` 板级（CO5300 屏/CST820 触摸/亮度/M5PM1 电池/M5IOE1 开关），来自 Muse 并对照 M5 官方示例，剥离依赖；其余模块不直接碰硬件。
- `pp_bus` / `pp_nav`：`*_parse.c` + `*_mock.c` 是纯 C，可在 Mac 上测；`bus.c`/`nav.c` 才依赖 IDF。
- `pp_storage`（NVS、收藏）、`pp_wifi`、`pp_http`、`pp_i18n`、`pp_ui`。
- `host/sim`：SDL2 桌面预览（`make -C host sim-run`），改 `pp_ui` 后先在这里看；`#ifdef PP_SIM` 的钩子只给模拟器用。
- `host/`：`make -C host test` 跑解析/收藏的断言，`make -C host demo` 打印 Mock 输出。

- 中文文案只写在 `pp_i18n/i18n.c` 和 `pp_nav/nav_parse.c`（字体生成脚本只扫这两个文件）；新增汉字后重跑 `tools/gen_cjk_font.sh`。

## 规则

- 改 `pp_bus`/`pp_nav` 的解析逻辑必须跑 `make -C host test`。
- 密钥只放 `sdkconfig.secrets`（git 忽略），不进源码。
- 导航界面必须保留“路线预览，非实时定位”横幅。
- 没编译、没上板验证过的东西，不要在文档里写成“已完成”。
