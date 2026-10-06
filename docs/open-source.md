# 开源协议与第三方

Boopie 是开源的。这一页说清楚哪些东西按什么协议用、哪些不在开源范围里，以及用 Muse 服务还要遵守什么。

## 一、代码：Apache License 2.0

- 整个仓库的代码按 [Apache License 2.0](../LICENSE) 发布，包括 Meta 原有的 muse-gadget-sdk 代码，以及 Boopie 新增和修改的代码
- 版权署名：Meta 原有文件保留 `Copyright (c) Meta Platforms, Inc. and affiliates.`；Boopie 新增或改过的文件写 `Copyright (c) 2026 Boopie contributors`，文件头都带 `SPDX-License-Identifier: Apache-2.0`
- 汇总的署名和说明在 [NOTICE](../NOTICE)

**你可以**：使用、修改、再发布、商用（代码本身），也可以闭源修改。
**你需要**：
1. 附上 LICENSE 和 NOTICE；
2. 改过的文件写明改过；
3. 不用 Meta、Muse 或 Boopie 的名字给自己的产品背书。

## 二、不在开源协议范围内的内容

| 内容 | 在哪里 | 说明 |
|---|---|---|
| Jollybot（Muse 默认形象） | `esp32/avatar/` | Meta 的角色，官方仓库就声明不在 Apache 协议覆盖范围内 |
| 品牌角色：GPT、Codex、小克、DeepSeek 小鲸鱼、豆包 | `components/boopie/avatar/` | 取材于各家品牌的像素致敬，版权和商标归各自所有者。原始标志、官方精灵图都不在仓库里 |
| 主题皮肤：海绵宝宝一家、蕾姆等 | 同上 | 版权归各自权利人 |

这些只供个人、非商业使用。做成产品送人或售卖时，要去掉它们，只留布比和自己原创的内容。权利人要求删除，我们会删。

## 三、第三方软件和字体

| 名称 | 用途 | 协议 | 位置 |
|---|---|---|---|
| [muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk) | 固件底座 | Apache-2.0 | 整个仓库 |
| [ESP-IDF](https://github.com/espressif/esp-idf) v6.0.1 及乐鑫组件 | 系统、驱动 | Apache-2.0 | 编译时下载 |
| [LVGL](https://github.com/lvgl/lvgl) | 界面 | MIT | 编译时下载（`managed_components/`） |
| [cJSON](https://github.com/DaveGamble/cJSON) | JSON | MIT | 编译时下载 |
| [joltwallet/littlefs](https://github.com/joltwallet/esp_littlefs) | 用户数据文件系统 | MIT | 编译时下载 |
| [minimp3](https://github.com/lieff/minimp3) | MP3 解码 | CC0-1.0 | `esp32/components/minimp3/` |
| Adafruit GFX glcdfont | 官方的 5×7 英文点阵 | BSD-2-Clause | `esp32/main/pixel_font.c` |
| [Noto Sans SC](https://github.com/notofonts/noto-cjk)（思源黑体） | 设置页等平滑中文 | SIL OFL 1.1 | `components/boopie/font/ui.otf`，协议 `OFL-NotoSansSC.txt` |
| [Ark Pixel Font](https://github.com/TakWolf/ark-pixel-font) | 像素中文 | SIL OFL 1.1 | 生成的点阵，协议 `OFL-ArkPixelFont.txt` |
| [Cubic 11](https://github.com/ACh-K/Cubic-11) | 像素中文 | SIL OFL 1.1 | 同上，协议 `OFL-Cubic11.txt` |
| [pypinyin](https://github.com/mozillazg/python-pinyin)、[jieba](https://github.com/fxsjy/jieba) | 拼音读音和字频 | MIT | 生成的表 `components/boopie/ime/` |

编译时下载的组件版本锁在 `esp32/dependencies.lock`。字体按 OFL 可以随固件分发，但不能单独出售，改过的字体不能再用原来的名字。

BLAKE3、Shadowsocks AEAD 等算法是照规范自己写的（`components/boopie/vpn/`），按 Apache-2.0 发布。

## 四、用 Muse 服务：另有条款

开源协议只管代码，不管 Muse 服务。接入 Muse 要遵守 [Gadget SDK Token 条款](https://gadgets.muse.ai/sdk-terms) 和 [Muse 服务条款](https://muse.ai/terms)，要点：

- 个人、非商业使用。一个 token 最多关联 50 台设备
- 不能把自己的 token 嵌进收费出售、公开上架或用于促销的设备。Boopie 固件里**不带任何 token**，每个人在板子上填自己的
- 不能暗示设备由 Meta 制造或背书
- 别人用你的设备前，要告诉对方设备怎么处理他的信息
- 不能绕过使用限制或安全措施

小智走的是小智官方服务器，按其服务条款使用。

## 五、我们的约定

- 仓库里不放任何密钥：Muse token、订阅链接、OTA 签名私钥都只在板子上或 GitHub Secrets 里
- 日志不打印 token，节点信息只显示名字
- 新增的源文件用下面的文件头：

```c
/*
 * Copyright (c) 2026 Boopie contributors
 * SPDX-License-Identifier: Apache-2.0
 */
```
