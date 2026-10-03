# 代码摸底说明

阶段 0 的产出之一：读官方 `muse-gadget-sdk` 的 ESP32 固件（上游提交 `1b56662`，2026-10-02），回答设计方案里留下的几个问题：界面怎么组织、OTA 从哪来、token 在哪用、命令怎么注册，并定下 Boopie 改动的挂钩点。

文中路径都相对于 `esp32/`。

## 一、总体结构

| 部分 | 位置 | 职责 |
|---|---|---|
| Home Link（底层） | `main/` | 蓝牙配对、Wi-Fi、和 Muse 云端 VM 的 Noise 加密会话、家庭网络隧道、设备命令、OTA |
| Muse 界面与语音 | `components/muse/` | LVGL 界面、形象、按键说话、设置页、语音对话 |
| 板子适配 | `components/muse/boards/board_*.c` | 每块板子一个文件，填一张 `muse_board_t` 表：屏幕、触摸、按键、音频、电源 |
| 默认形象 | `avatar/muse_pixel.c` | Jollybot 像素形象（Meta 的角色，不在 Apache 协议内） |
| 主机测试 | `tests/` | 在电脑上编译 C 代码跑单元测试，不需要板子 |
| UI 模拟器 | `simulator/` | 在电脑上用 SDL 跑真实的 `muse_ui.c`，可无头截图 |

启动顺序：`main/main.c` → `muse_glue_start()` → `main/app.c` 里的 Home Link 主流程；带屏板子由 `muse_app_run(board)`（`components/muse/muse_app.c`）拉起界面、输入、语音和 Muse 客户端。

微雪 1.75C 编译时叠加三层配置：`sdkconfig.defaults` → `devices/sdkconfig.muse`（所有带完整界面的板子）→ `devices/sdkconfig.muse-waveshare-s3-175c`（本板）。

## 二、界面组织

- `muse_ui.c` 的 `build_screen()` 用 LVGL **tileview** 建了两页：第 0 页是形象（face），向左滑是第 1 页设置页（`muse_settings_ui.c`）。没有触摸的板子改用 `muse_menu.c` 的按键菜单。
- 一个 LVGL 定时器 `frame_tick` 每 40 ms 刷新一次（约 25 帧），1.75C 的配置也是 40 ms。
- 形象由 `muse_pixel_render()` 画在 64×64 的网格上，再用 `muse_pixel_scale()` 按条带放大输出，**整屏图不进内存**。在 466×466 屏上实际放大到 320 px（每格 5×5），不是设计方案里估计的 7×7。
- 状态来自 `muse_state.h`：7 个模式（boot、idle、listening、thinking、speaking、error、off）加一个 0..1 的“开心值”（被摸时）。正好对应 Boopie 的 8 个核心表情。
- `display.draw_url` 推来的图片盖在形象上，点一下、按键或打开菜单就消失。
- 屏幕上的状态文字用 LVGL 内置的 Montserrat 和 unscii 字体，**只有拉丁字符**（见第七节）。

**Boopie 挂钩点**：在 `build_screen()` 里给 tileview 加页（宠物主界面、游戏、表盘），或者把第 0 页的内容换成宠物主界面。tileview 本身支持任意多页，改动集中在这一个函数。

## 三、SDK token 在哪里用

- 编译时写死：Kconfig 项 `CONFIG_GADGET_SDK_TOKEN`（`main/Kconfig.projbuild`）。
- **唯一的读取点**是 `main/identity.c` 的 `identity_sdk_token()`，配对（`main/link_pairing.c`）和刷新设备 token 都通过它拿。
- `cmake/validate_config.cmake` 在编译时检查格式（`mgst_` 开头、共 48 个字符）。为空时只警告，照样出固件，只是不能配对。
- 云端编译时，工作流把 Secret `MUSE_SDK_TOKEN` 写进构建目录里的一个临时配置层，编完就删，不进仓库也不打印。

**阶段 2“token 用户自填”的改法**：只改 `identity_sdk_token()` 一个函数：先读 NVS，没有再退回 Kconfig。返回值要求指向静态存储（见 `link_pairing.c:83` 的注释），所以 NVS 里读出来的要放进静态缓冲区。

## 四、OTA 从哪里来

和设计方案的假设不同：**设备自己从不检查更新**，OTA 全部是外部推送的：

1. Muse 云端通过加密会话发 `device.ota` 命令，带 `url` 和可选的 `force`（`main/noise_control.cpp`）；
2. 手机配网时，蓝牙配网参数里可以附带 OTA 地址（`main/app.c` 的 `start_wifi_join_ota_if_requested`）。

两条路都走 `main/ota.c` 的 `ota_start(url, force, cb, user)`：

- **版本门槛**：`version_is_newer()` 比较语义化版本，自编译固件默认 `999.0.0`（`version.txt`），官方推来的低版本镜像会被跳过，除非 `force`。
- **签名校验**：`CONFIG_SECURE_SIGNED_ON_UPDATE_NO_SECURE_BOOT=y`，只接受用 `CONFIG_SECURE_BOOT_SIGNING_KEY` 那把私钥签名的镜像。现在用的是仓库里**公开的**开发密钥 `dev_signing_key.pem`；官方正式固件用的是 Meta 自己的密钥，所以官方 OTA 本来就装不上我们的板子。
- **启动失败回滚**：官方已经开启（`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`），新固件启动后要在限定时间内连上控制会话，`ota_verify_task` 才标记为有效，否则下次重启退回旧槽。设计方案里“打开回滚”这一项不用再做。
- 1.75C 默认开启 OTA（`devices/sdkconfig.muse` 里 `CONFIG_HOMEHUB_OTA_ENABLED=y`），两个程序槽各 4 MB。

**对“官方 OTA 覆盖”这个风险的结论**：基本不存在。版本号 999.0.0 和签名密钥两道门都会挡住。

**阶段 2 自建 OTA 要做的**：

- 换成我们自己的签名私钥（存 GitHub Secrets，CI 里写成文件，`CONFIG_SECURE_BOOT_SIGNING_KEY` 指过去）。公开的开发密钥谁都能拿来签名，正式用之前必须换。注意：换钥匙后第一次要用 USB 刷，旧固件不认新钥匙签的镜像。
- 新增一个“拉取”任务：定时访问 Cloudflare Worker 的版本清单，发现新版本就调 `ota_start()`。这是纯新增代码，不改 `ota.c`。
- 版本号改成由 CI 生成并单调递增（`-DPROJECT_VER=...`），不能再全是 999.0.0。

## 五、设备命令怎么注册

分两处，都在 `main/`：

1. **声明**：`noise_control.cpp` 里拼装 `node.register` 消息，用 `add_command(commands, 名称, 描述, 必填参数, 可选参数)` 逐条加，可以附带 `timeout_ms`。Muse 就是从这里知道板子会什么。
2. **分发**：`app.c` 的 `on_ws_command(command, params, request_id, session_generation)` 按名字 `strcmp` 分发，返回 cJSON 结果；耗时操作返回 `{"_async": true}`，做完再用 `noise_ctrl_send_command_result()` 回结果。

`device.ota` 和 `device.health` 在 `noise_control.cpp` 内部先处理，其余的才交给 `on_ws_command`。

**Boopie 挂钩点**：在两处各加一行，调用 `components/boopie/` 里的

- `boopie_commands_describe(cJSON *commands)`：追加 Boopie 的命令声明；
- `boopie_commands_handle(...)`：先给 Boopie 处理，认不出的再交回原来的分发。

这样设计方案里的“能力清单”只需在 Boopie 组件里定义一次，同时导出成 Muse 命令和小智的 MCP 工具。

## 六、和小智（国内大脑）的关系

- Muse 客户端在 `components/muse/muse_chat*`（`CONFIG_MUSE_HATCH` 时用 `muse_chat.c` + `muse_chat_session.cpp`，否则 `muse_chat_link.c`）。语音管线在 `muse_voice.c`，它只关心“开始听 / 发音频 / 收到回复 / 状态变化”，正好是设计方案里“大脑接口”要抽出来的那一层。
- 抽接口时以 `muse_voice.c` 和 `muse_chat.h` 之间的调用为边界，另写一个小智协议客户端实现同一组函数。

## 七、新发现的问题

1. **没有中文字体**。固件只编了 Montserrat 和 unscii（`devices/sdkconfig.muse`），中文显示成方框，模拟器截图已确认。国内大脑回中文、宠物界面写中文都要先解决：用 LVGL 字体转换器做常用 3500 字的子集位图字体，或者把 TTF 放进 Flash 用 `tiny_ttf` 实时渲染。属于阶段 3 的前置工作。
2. **32 MB Flash 只用了一半**。1.75C 有 32 MB Flash，但沿用的 `partitions_muse.csv` 按 16 MB 规划，实际只分到约 8.2 MB。剩下的空间够放中文字体、高清形象序列帧和形象包。后面要加一张 Boopie 自己的分区表（加一个资源分区），注意**不能动现有分区的偏移**，否则 OTA 升级后分区表对不上。
3. **陀螺仪 QMI8658 确实没用**。1.75C 的板子文件里没有任何 IMU 代码，“布比接零食”要从驱动开始写（可参考微雪官方示例仓库里的 QMI8658 例程）。
4. **官方带了 UI 模拟器**。`simulator/` 能在电脑上跑真实界面代码并截图。Boopie 给它加了 1.75C 档位（`-DMUSE_SIM_WAVESHARE_175C=ON`，466×466，按键提示位置和真实板子一致），CI 每次都会生成截图。宠物主界面、表情、小游戏的界面部分到货前就能开发和预览。

## 八、Boopie 已落地的代码

| 内容 | 位置 | 说明 |
|---|---|---|
| 表情规范 | `components/boopie/boopie_expr.[ch]` | 8 个核心 + 10 个扩展表情、按名字查找、退回规则；前 7 个编号与 `muse_mode_t` 一致，可直接转换 |
| 单元测试 | `components/boopie/tests/` | 对照设计方案的表格逐项检查，CI 自动跑 |
| 云端编译 | `.github/workflows/boopie.yml` | 1.75C 固件（合并整包 + 分段文件）、Boopie 与官方的主机测试、模拟器截图 |
| 模拟器 1.75C 档位 | `simulator/` | 见上 |
| 截图脚本 | `../tools/boopie/sim_shots.py` | 渲染 8 种状态并拼成总览图 |

对上游文件的改动只有三处，都很小：`simulator/CMakeLists.txt` 和 `simulator/src/sim_board.c`（1.75C 档位），以及把官方两个工作流改成手动触发。
