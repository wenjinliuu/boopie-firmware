# Boopie 布比

一只住在圆形小屏里的电子宠物，也是会聊天的 AI 小伙伴。

它会饿、会困、会长大，有自己的小世界、小游戏和一柜子皮肤；按住按键说话，就能找 **Muse** 或 **小智** 聊天。Boopie 从 Meta 开源的 [muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk) 改起，跑在微雪 ESP32-S3-Touch-AMOLED-1.75C 圆屏开发板上。

> 这是个人爱好项目，不是 Meta 的产品，也没有得到 Meta 的认可或背书。

## 能做什么

| | |
|---|---|
| **电子宠物** | 主屏就是布比：心情、饥饿、等级和星星；戳它、摸它、抱起来晃一晃都有反应，偶尔还会自言自语 |
| **两个 AI 助手** | Muse（Meta，需要自己的开发者 token 和能访问海外的网络）或小智（国内，直连）；按键说话，回复显示在屏幕上 |
| **小世界** | 屋里、家门口和农场、森林、海边；种地、钓鱼、捡贝壳、开宝箱，家具随等级变多；天气和节日会变 |
| **小游戏** | 戳戳布比、接零食、重力迷宫、跳跳布比，挣来的星星给宠物买零食和皮肤 |
| **换装** | 7 个角色、几十套皮肤、配饰、颜色和背景；有节日限定和成就皮肤 |
| **实用小工具** | 白噪音、聊天记录、相册、断网留言、九宫格拼音输入法 |
| **自带 VPN** | 板载 Shadowsocks，导入订阅后只让 Muse 和系统更新走代理，其他直连 |
| **在线更新** | 每天检查一次，有新版本时宠物提醒你，点两下才安装；装坏了自动退回旧版本 |

更细的设计见 [docs/](docs/)，从 [设计方案](docs/design.html) 看起。

## 硬件

[微雪 ESP32-S3-Touch-AMOLED-1.75C](https://www.waveshare.net/shop/ESP32-S3-Touch-AMOLED-1.75C.htm)：ESP32-S3R8、8 MB PSRAM、32 MB Flash、1.75 寸 466×466 圆形 AMOLED、触摸、双麦克风、扬声器、六轴陀螺仪、电池接口。

## 上手

1. **下载固件**：到 [Releases](https://github.com/wenjinliuu/boopie-firmware/releases) 下载最新的完整烧录包：
   - `boopie-<版本>-merged.bin`：正常版；
   - `boopie-<版本>-unlocked-merged.bin`：解锁版，所有皮肤、配饰、颜色和背景直接能用。
2. **第一次刷机**：用 USB 线从地址 `0x0` 刷入，命令行或 Chrome / Edge 的[网页刷机工具](https://espressif.github.io/esptool-js/)都行。步骤和原厂固件备份见 [刷机手册](docs/flashing.md)。
3. **开机引导**：板子会一步步带你连 Wi-Fi、选 AI 助手（Muse 或小智）、填 token 和 VPN、配对。几十位的密钥在手机上粘贴：**设置 › 手机扫码设置**。
4. **以后的更新**：不用再插线。**设置 › 系统更新** 里能看到版本、更新说明，也能在正常版和解锁版之间切换，存档不受影响。

### 用 Muse 需要什么

- 在 [gadgets.muse.ai](https://gadgets.muse.ai/settings/sdk-tokens) 生成自己的开发者 token（`mgst_` 开头），在手机设置页里粘贴。固件里不带任何人的 token，同一份固件谁都能用
- 能访问海外的网络：在家用的 Wi-Fi 能直连，或者在板子的 **设置 › VPN** 里导入 Shadowsocks 订阅
- Muse App 里打开 **设置 › 设备 › 开发者模式**，添加设备后按一下板子上的键确认

用小智不需要这些，按屏幕上的激活码到小智官网绑定就行。

## 自己编译

推送到任意分支，GitHub Actions 会编译固件、跑测试、截模拟器图（**Actions › Boopie firmware**），产物在那次运行的 Artifacts 里。本地编译、模拟器、发布新版本，见 [维护手册](docs/maintenance.md)。

## 仓库结构

| 目录 | 内容 |
|---|---|
| `esp32/` | 固件。Muse 官方代码，Boopie 的改动尽量只落在少数挂钩点 |
| `esp32/components/boopie/` | Boopie 自己的代码：宠物、形象、小世界、游戏、字体、输入法、VPN、小智、在线更新 |
| `esp32/simulator/` | 电脑上跑的界面模拟器，1.75 寸圆屏 |
| `cloud/ota-worker/` | 在线更新服务器（Cloudflare Worker + R2） |
| `tools/boopie/` | 生成美术、字库、资源包，以及发布和签名脚本 |
| `docs/` | 设计和维护文档 |
| `linux/`、`skills/` | 官方的 Linux SDK 等，暂不使用，保留以便合并上游 |

本仓库带着官方仓库的完整历史，官方仓库作为上游 `upstream` 跟进更新。

## 开源协议

- 代码按 [Apache License 2.0](LICENSE) 发布，包括 Meta 原有代码和 Boopie 新增的代码；版权与署名见 [NOTICE](NOTICE)
- 字体按 SIL Open Font License 1.1，其他第三方代码保留各自的协议，清单见 [开源协议与第三方](docs/open-source.md)
- **不在开源协议范围内**：Meta 的默认形象 Jollybot（`esp32/avatar/`），以及 GPT、Codex、小克、小鲸鱼、豆包等品牌角色和海绵宝宝、蕾姆等主题皮肤。它们是爱好者的像素致敬，版权归各自权利人，只供个人使用，不能用于商业用途
- 用 Muse 服务还要遵守 [Gadget SDK Token 条款](https://gadgets.muse.ai/sdk-terms)：个人、非商业使用；开源协议不授予任何 token 权利

欢迎提 issue 和 pull request，见 [贡献指南](CONTRIBUTING.md)。

作者：[wenjinliuu](https://github.com/wenjinliuu)
