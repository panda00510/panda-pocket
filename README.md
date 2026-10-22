# Panda Pocket

M5Stack StopWatch（C152：ESP32-S3R8，1.75 寸 466×466 圆形 AMOLED 触摸屏，自带 450 mAh 电池）上的口袋小工具：
Panda 表情主页、新加坡公交到站、OneMap 步行路线预览、设置（Wi-Fi / 亮度 / 中英文 / 状态）。
ESP-IDF + LVGL，**不依赖任何 Muse 云服务**，无 AI 语音、无账号体系。

> **当前状态：V1 框架。** 固件在 ESP-IDF 6.0.1 下编译通过；**从未烧录、从未在真机上运行**。
> 已验证与未验证的清单见 [PROGRESS.md](PROGRESS.md)，本文凡标"未验证"的步骤都没在硬件上跑过。

## 目录

1. [没有硬件也能做的事](#1-没有硬件也能做的事已验证)
2. [构建固件](#2-构建固件)
3. [刷机（未在真机验证）](#3-刷机未在真机验证)
4. [首次上板检查清单](#4-首次上板检查清单)
5. [接入真实 API](#5-接入真实-api)
6. [使用说明](#6-使用说明)
7. [项目结构、许可证](#7-项目结构与许可证)

---

## 1. 没有硬件也能做的事（已验证）

Mac 上即可，不需要 ESP-IDF。

```bash
# 逻辑测试 + 终端 Mock 演示（需要 cc、make、git；会克隆固定版本 cJSON v1.7.18）
make -C host test      # 公交/导航解析、Mock 路线、Wi-Fi 列表整理、收藏逻辑的断言
make -C host demo      # 终端打印 Mock 公交到站 + 路线预览

# 桌面界面预览：真实的 components/pp_ui/ui.c + SDL2，466×466 窗口，Mock 数据
brew install cmake ninja sdl2
make -C host sim-run                 # 打开窗口：鼠标=触摸，向右拖动=返回
make -C host sim-shots               # 无窗口截图到 host/build/shot_*.bmp
host/build/sim/sim --lang zh         # 中文界面
PP_SIM_ERR=auth|net|empty / PP_SIM_WIFI_ERR=auth / PP_SIM_CHARGING=1 host/build/sim/sim ...   # 模拟各种错误和充电状态
PP_SIM_ROUTE_JSON=host/tests/onemap_walk.json host/build/sim/sim --screen route   # 用真实 OneMap 响应看路线图
make -C host sim-check               # ASan/UBSan：所有界面中英文各跑一遍，崩溃/越界即失败
host/build/sim/sim --shot out.bmp --taps "233,233 233,150"   # 脚本化点击（像素坐标）
```

模拟器**不能**验证：真实触摸手感、帧率、AMOLED 色彩、PSRAM/内存压力、Wi-Fi、NVS、低功耗。

## 2. 构建固件

**环境**（已在 macOS / Apple Silicon 上验证编译）

| 项 | 版本 |
|---|---|
| ESP-IDF | **v6.0.1**（组件声明 `>=5.5`；Muse 上游以 6.0.1 验证） |
| 目标 | `esp32s3`，16 MB Flash，8 MB 八线 PSRAM |
| LVGL | 9.5.0 |
| esp_lvgl_adapter | 0.6.4 |
| esp_lcd_co5300 | ^2.2.0 |
| cjson | ~1.7 |
| 工具 | cmake、ninja、dfu-util、Python 3（IDF 安装脚本自带虚拟环境） |

组件依赖写在各 `idf_component.yml`，首次 build 自动下载，无需手动安装。

**安装 ESP-IDF（macOS）**

```bash
brew install cmake ninja dfu-util
mkdir -p ~/esp && cd ~/esp
git clone -b v6.0.1 --depth 1 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3
```

**编译**

```bash
. ~/esp/esp-idf/export.sh          # 每个新终端都要执行
cd "<本项目目录>"
idf.py set-target esp32s3          # 只需第一次
idf.py build
```

成功时末尾显示 `Project build complete`，固件 `build/panda-pocket.bin` 约 1.3 MB（4 MB 分区占 32%）。
含空格的项目路径实测可以编译。

**默认是 Mock Mode**（`CONFIG_PP_MOCK_MODE=y`）：不需要 Wi-Fi、不需要 API Key 就能启动，公交与路线显示内置模拟数据，界面上标注 "MOCK DATA"。

## 3. 刷机（未在真机验证）

下面的硬件步骤来自 [M5Stack 官方文档](https://docs.m5stack.com/en/core/StopWatch)，**我没有硬件，没有实际做过**。

### 3.1 先看一眼板子版本（重要）

M5 文档警告：**v1.0 版本的部分板子丝印有误，标 `BAT` 的引脚实际是 5V IN，千万不要接电池；v1.0.1 才是正确的 BAT。**
本固件不需要你外接电池（板子自带）、也不需要动这些引脚。如果你要接别的东西，先确认版本。

### 3.2 进入下载模式

1. 用 USB-C **数据线**（不是纯充电线）连接电脑。
2. **按住电源键约 2 秒，直到绿色 LED 亮起**，然后松开。设备此时处于下载模式。
3. 查看串口名：
   ```bash
   ls /dev/cu.usbmodem*
   ```
   通常会出现类似 `/dev/cu.usbmodem1101` 的设备（ESP32-S3 原生 USB）。没出现：换线、换口、重新进入下载模式。

> 其他操作：短按电源键一次 = 开机/复位；快速连按两次 = 关机（M5 文档）。
> 注意本固件启动时会关闭电源键的"单击复位"（照搬 M5 出厂固件的做法），所以烧录后要复位，用 `idf.py` 的自动复位，或重新插拔 USB。

### 3.3 烧录（方式 A：idf.py，推荐）

```bash
. ~/esp/esp-idf/export.sh
idf.py -p /dev/cu.usbmodem1101 flash monitor     # 换成你的串口
```

退出串口监视器：`Ctrl+]`。

### 3.4 烧录（方式 B：单个合并文件 + esptool）

```bash
idf.py merge-bin                                  # 生成 build/merged-binary.bin（已验证可生成）
python -m esptool --chip esp32s3 -p /dev/cu.usbmodem1101 -b 460800 \
    write-flash --flash-mode dio --flash-size 16MB --flash-freq 80m 0x0 build/merged-binary.bin
```

### 3.5 烧录失败排查

| 现象 | 处理 |
|---|---|
| 找不到串口 | 换数据线/USB 口；重新按住电源键进入下载模式；必要时装 M5 页面提供的 Device USB Driver |
| `Failed to connect` | 重新进入下载模式再烧；降低速度：`-b 115200` |
| 烧完黑屏 | 先看串口日志（`idf.py monitor`）；对照 [4. 首次上板检查清单](#4-首次上板检查清单) |
| 反复重启 | 看日志里的 backtrace；很可能是 PSRAM/驱动初始化问题（见下） |

## 4. 首次上板检查清单

这些都是**未验证的预期行为**，按顺序查，遇到问题把串口日志贴出来最有用。

1. **能启动**：串口出现 `panda` 日志；没有 `display failed to start`。
2. **屏幕亮**：出现熊猫脸。黑屏优先查：OLED 供电轨（M5IOE1 的 IO8）、屏幕复位、`esp_lcd_co5300` 初始化日志、x 偏移 6。画面左右错位也是这个偏移的问题。
3. **触摸**：点击首页进入菜单；右滑返回。触摸无响应优先查 CST820（I2C 0x15）、触摸复位（M5IOE1 IO4）；坐标镜像/旋转需要改 `tp_read`。
4. **PSRAM**：日志应显示 8 MB。没有则检查 `sdkconfig.defaults` 的八线 PSRAM 配置。
5. **亮度**：设置里拖动滑块，屏幕明暗变化。
6. **电量**：首页显示百分比。电量曲线是估算（沿用 Muse 的拟合），需要和实际对照校准；没有读充电/USB 状态。
7. **Wi-Fi**：设置 → Wi-Fi → 扫描 → 选网络 → 输入密码。成功后首页 Wi-Fi 图标变化，时间显示新加坡时间（需要 SNTP 对时成功）。密码输错目前没有失败提示，状态会一直显示"连接中"。
8. **息屏/低功耗**：空闲到一半时间先变暗到 15%，到点息屏（默认 30 秒，`menuconfig` → Panda Pocket → Screen off）。息屏后 UI 暂停、触摸芯片睡眠、芯片自动 light sleep。**唤醒只能按侧键（黄/蓝）或单击电源键，触摸唤不醒**（这块板的触摸中断引脚不明）。检查：息屏后按键能否唤醒；唤醒后画面是否完整重绘；USB 连着时**不会**进入 light sleep（刻意如此，否则串口会掉线），所以功耗要拔掉 USB 用电池测。关闭：`CONFIG_PP_LOW_POWER=n`，则只关背光并让面板休眠，触摸可唤醒，但耗电高。
9. **Mock 数据**：公交页与导航页显示 "MOCK DATA"。
10. 之后再接真实 API（第 5 节）。

## 5. 接入真实 API

**LTA DataMall**（公交到站）

- 在 <https://datamall.lta.gov.sg/> 注册并申请 AccountKey（免费，需审批）。
- 使用接口：`BusArrival v3`，请求头 `AccountKey`。
- 请求头 `AccountKey`，接口 `https://datamall2.mytransport.sg/ltaodataservice/v3/BusArrival`。Key 不对时接口返回 401。
- **LTA 会给你两个 Key：`API Account Key`（24 位左右，base64 样式，结尾可能有 `=`）才是我们要填的；`SDK Account Key`（32 位十六进制）是给 LTA 另一个 SDK 用的，填它会得到 401。**
- 已用真实响应验证（2026-10-22）：解析代码正确，真实响应存为 `host/tests/lta_busarrival.json`。
- 我**没有查证**官方的频率限制；固件保守地对同一站点至少间隔 20 秒，界面每 30 秒刷新。上线前请读官方文档。

**OneMap**（步行路线）

- 在 <https://www.onemap.gov.sg/apidocs/> 注册，用邮箱和密码换取 token（token 会过期，约 3 天）。
- 使用接口：`routingsvc/route`，`routeType=walk`。
- **已用真实响应验证（2026-10-22）**：响应含 `route_summary`（总距离/时间）、`route_geometry`（精度 5 的 polyline）、`route_instructions`（每步 10 个字段，可读文字在第 10 个，路名常为空）；解析代码已按此修正，并有真实响应的测试数据 `host/tests/onemap_walk.json`。
- 鉴权：`Authorization` 头带原始 token 或加 `Bearer` 前缀实测都返回 200，固件用前者。
- **token 有效期 3 天**（JWT 里 `exp` 减 `iat` 实测为 3.0 天），过期后接口会拒绝，需要重新获取并更新 `sdkconfig.secrets`。固件里的 token 是编译时写死的，没有自动续期。

**配置**

```bash
cp sdkconfig.secrets.example sdkconfig.secrets     # 已被 git 忽略
# 编辑 sdkconfig.secrets：填 CONFIG_PP_LTA_ACCOUNT_KEY / CONFIG_PP_ONEMAP_TOKEN，
# 并设 CONFIG_PP_MOCK_MODE=n（可选：CONFIG_PP_WIFI_SSID / CONFIG_PP_WIFI_PASSWORD 作为默认 Wi-Fi）
rm -f sdkconfig && idf.py build
```

**安全说明**：Key 会被编进固件二进制，仅限自用开发，不要分发这个固件。商用请改为后端代理，密钥只放后端 `.env`。无 Key 或开着 Mock 时一律用 Mock 数据。

## 6. 使用说明

| 操作 | 说明 |
|---|---|
| 主页 | 熊猫眼动画、新加坡时间、电量、Wi-Fi 状态。点击进入菜单 |
| 返回 | 任意子界面向右滑 |
| 自动息屏 | 空闲 15 秒先变暗，30 秒息屏（可在 `idf.py menuconfig` → Panda Pocket 里改）；按侧键或单击电源键唤醒 |
| **公交** | 列表显示收藏站点和"+ 新增"；新增：数字键盘输入 5 位站号，满 5 位后确认键才可点；到站页显示各路线下一班/第二班的分钟数和拥挤度（seat 有座 / stand 站位 / full 拥挤）；底部"收藏/已收藏"按钮保存或取消收藏（NVS，最多 8 个）；每 30 秒自动刷新 |
| **导航** | 选择起点和终点（4 个预设地点：Raffles Place MRT、Esplanade、Gardens by the Bay、Orchard MRT），点出发。显示距离、预计步行分钟、转向提示（带每步距离）和折线示意图（起点 A 绿色、终点 B 红色）（起点 A 绿色、终点 B 红色）。**始终标注"路线预览，非实时定位"——本设备没有 GPS** |
| **设置** | 亮度滑块、语言（English / 中文，即时生效）、Wi-Fi 配置、电池/Wi-Fi/接口状态 |

已知限制：路线步骤文字和屏幕键盘只有英文；Mock 路线是 L 形街格估算，与真实路线有出入（实测 Raffles→Esplanade 估 1078 m / 真实 1028 m；Raffles→Gardens 估 1613 m / 真实 2044 m）；OneMap 步行路线的步骤没有路名，只有"Turn Right + 距离"；无隐藏 SSID、无企业 Wi-Fi 支持。完整清单见 [PROGRESS.md](PROGRESS.md)。

## 7. 项目结构与许可证

结构、约定、模块职责见 [AGENTS.md](AGENTS.md)。

- 许可证：Apache-2.0（[LICENSE](LICENSE)）。
- 来源声明（Muse Gadget SDK 的板级代码、M5Stack 示例的交叉核对、Noto Sans CJK 字体）见 [NOTICE](NOTICE)。
- 修改中文文案后需重新生成字体子集：`tools/gen_cjk_font.sh <NotoSansCJKsc-Regular.otf>`（需要 node）。
