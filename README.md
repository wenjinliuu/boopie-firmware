# boopie-firmware

Boopie（布比）：一块圆屏小设备上的多应用系统。Muse 是常驻的 AI 应用，同时支持国内大模型（双大脑），主界面是电子宠物，另有表盘、小游戏和信息卡片。

- 主板：微雪 ESP32-S3-Touch-AMOLED-1.75C
- 第二板：乐鑫 ESP-Mosaico（ESP32-S31）
- 起点：Meta 开源的 [muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk)（Apache 2.0）

## 文档

- [设计方案](docs/design.html)：项目定位、Muse 官方发布与条款、两块硬件、双大脑架构、形象与表情规范、应用规划、阶段路线、风险和参考资料
- [代码摸底说明](docs/code-survey.md)：官方固件的界面组织、OTA 来源、token 使用位置、命令注册方式，以及 Boopie 的挂钩点
- [中英文字系统](docs/text-system.md)：底层同时支持中文和英文：像素字体、按列排版、中文断行
- [到货操作手册](docs/flashing.md)：备份原厂固件、刷入、配对、验证清单

GitHub 不会直接渲染 HTML，下载后用浏览器打开即可；也可以开启 GitHub Pages，通过网页访问。

## 仓库结构

本仓库带着官方仓库的完整历史，官方仓库设为上游 `upstream`，跟进官方更新：

```sh
git remote add upstream https://github.com/facebookincubator/muse-gadget-sdk.git
git fetch upstream main
git merge upstream/main
```

| 目录 | 内容 |
|---|---|
| `esp32/` | 官方 ESP32 固件（Boopie 改动尽量只落在少数挂钩点） |
| `esp32/components/boopie/` | Boopie 自己的代码，独立组件 |
| `linux/`、`skills/` | 官方的 Linux SDK 等，暂不使用，保留以减少合并冲突 |
| `docs/` | Boopie 文档 |
| `.github/workflows/boopie.yml` | Boopie 云端编译：微雪 1.75C 固件 + 单元测试 |

官方原有的 CI（`esp32.yml`、`linux.yml`）改成只能手动触发，避免每次提交都编 11 块板子和 macOS 模拟器。

## 云端编译

推送到任意分支或手动运行 **Actions › Boopie firmware** 即可编译。产物在该次运行的 Artifacts 里：

- `boopie-waveshare-s3-175c-merged.bin`：合并好的整包，网页刷机工具从地址 `0x0` 刷入
- `flash/`：分段文件和 `flash_args`，给 `esptool` 命令行用

## 约定

- Muse SDK token 只存放在仓库 Secrets，名称 `MUSE_SDK_TOKEN`，不写进代码。没有配置时也能编译，但固件无法配对
- 本项目不是 Meta 的产品，也未获得 Meta 背书

官方原 README 见 [muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk#readme)。许可证：官方代码按 [LICENSE](LICENSE)（Apache 2.0），第三方文件保留各自许可；`esp32/avatar/` 里的默认形象 Jollybot 不在 Apache 协议覆盖范围内。
