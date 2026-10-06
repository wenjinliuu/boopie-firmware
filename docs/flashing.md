# 刷机手册

第一次把 Boopie 刷进微雪 ESP32-S3-Touch-AMOLED-1.75C，按顺序做。全程大约一小时，大半时间在备份原厂固件。刷过一次之后，以后的版本都在板子上在线更新（**设置 › 系统更新**），不用再插线。

## 到货前准备

- [ ] 在 [gadgets.muse.ai](https://gadgets.muse.ai/settings/sdk-tokens) 生成 SDK token（开发者 token，`mgst_` 开头），先存在自己手机上。它不进代码、不发聊天，也**不再放进固件**：刷好机后在板子的 **设置 › 手机扫码设置** 里粘贴
- [ ] 到 [Releases](https://github.com/wenjinliuu/boopie-firmware/releases) 下载最新的完整烧录包：`boopie-<版本>-merged.bin`（正常版）或 `boopie-<版本>-unlocked-merged.bin`（解锁版，皮肤全开）。两种以后可以在板子上互换
- [ ] 用小智的话，什么都不用准备，开机后按屏幕上的激活码去绑定
- [ ] Windows 上装 [Python 3](https://www.python.org/downloads/)（勾选 “Add python.exe to PATH”），然后在终端执行 `pip install esptool pyserial`
- [ ] 一根能传数据的 USB-C 线（只能充电的线连不上）
- [ ] 手机装好 Muse App，打开 **设置 › 设备 › 开发者模式**

## 1. 连上电脑，确认端口

1. 用 USB-C 线把板子接到电脑。
2. 打开“设备管理器 › 端口 (COM 和 LPT)”，应出现一个 **USB 串行设备 (COMx)**，记下编号，下文用 `COM5` 代替。
3. 确认芯片：

   ```
   python -m esptool --chip esp32s3 -p COM5 chip-id
   ```

   输出里有 `ESP32-S3` 和 MAC 地址即可。连不上就进下载模式：**按住 BOOT 键，拔插 USB 线，再松开 BOOT**，然后重试。

## 2. 备份原厂固件（必做）

整块 32 MB Flash 读出来，以后随时能刷回原厂状态：

```
python -m esptool --chip esp32s3 -p COM5 -b 921600 read-flash 0 0x2000000 waveshare-175c-factory.bin
```

- 需要几分钟，中途不要拔线
- 完成后文件应正好是 33,554,432 字节
- 备份文件存到网盘或其他电脑一份。**不要提交到 GitHub**，里面可能有原厂的 Wi-Fi 信息

## 3. 刷入 Boopie 固件

用到货前下载的合并包（下面以 `boopie-1.0.0-merged.bin` 为例）。第一次刷机先整片擦除，避免原厂固件残留的数据和 Boopie 冲突：

```
python -m esptool --chip esp32s3 -p COM5 -b 921600 erase-flash
python -m esptool --chip esp32s3 -p COM5 -b 921600 write-flash 0x0 boopie-1.0.0-merged.bin
```

不想用命令行，也可以用 Chrome / Edge 打开 [esptool-js 网页刷机](https://espressif.github.io/esptool-js/)：Connect → Erase Flash → 地址填 `0x0`、选合并包 → Program。

刷完按一下 PWR 键或拔插 USB 重启。

> 以后的版本走在线更新。真要用线刷新版本，**不用擦除**，直接 `write-flash`，配对、Wi-Fi、宠物存档都会保留。

> **分区表**：Boopie 用自己的 `esp32/partitions_boopie_32mb.csv`，把 32 MB 全部用上（详见[存储设计](boopie-storage.md)）：
> - 两个程序区，各 6 MB，在线更新时轮流用；
> - 128 KB 崩溃记录；
> - 3.75 MB 资源区：字体、像素字库、小世界背景；
> - 约 15.8 MB 用户数据区：宠物存档、聊天记录、相册、留言、VPN 节点；
> - 设置区等位置和官方一样。
>
> 分区表只能通过 USB 刷机写入，在线更新改不了，所以第一次就刷这张表。
>
> 合并包含资源区，文件约 16 MB，大部分是空白；esptool 会压缩传输，不会慢多少。

## 4. 看启动日志

```
python -m serial.tools.miniterm COM5 115200
```

（按 `Ctrl+]` 退出。）正常启动能看到：

- `link.main: Muse Gadget starting`
- `muse: board: Waveshare ESP32-S3-Touch-AMOLED-1.75C`
- `muse: ready: free heap ... internal, ... psram`

整段日志复制下来存成文本，后面要用。看到 `panic`、`abort` 或反复重启，把日志存下来，[提 issue](https://github.com/wenjinliuu/boopie-firmware/issues) 时附上（先去掉密码和 token）。

## 5. 跟着开机引导走

第一次开机会进入新手引导：连 Wi-Fi、选 AI 助手、填 token 和 VPN、配对，每一步都有说明，做过的会自动跳过。中途退出了，可以在 **设置 › 新手引导** 重新开始。下面是用 Muse 时的手动做法：

1. 主屏左滑到设置，点 **手机扫码设置**，手机扫屏幕上的码连上板子的热点，在弹出的网页里：
   - 填 Wi-Fi；
   - 在 **Muse › 开发者 token** 粘贴到货前准备好的 `mgst_` token（保存后板子会重启一下）；
   - 在 **VPN** 粘贴 Shadowsocks 订阅链接。
2. 板子 **设置 › VPN**：打开开关，点“更新订阅”，选一个节点（可以先点“测速”）。
3. Muse App：**设置 › 设备 › 添加设备**（右上角 **+**），选板子的名字（**设置 › 蓝牙** 里能看到）。
4. 屏幕提示时，按一下板子上面的键确认。设备 token 由配对自动拿到，不用填。

## 6. 验证清单

逐项记录结果，交回来决定后续顺序：

| # | 项目 | 怎么验证 | 结果 |
|---|---|---|---|
| 1 | 启动与屏幕 | 形象正常显示、动画流畅 | |
| 2 | 触摸 | 戳形象会开心；向左滑到设置页 | |
| 3 | 配对 | App 里出现设备，屏幕上设备名消失、出现麦克风图标 | |
| 4 | **国内网络能否连上 Muse** | 配好 Wi-Fi 后状态是否变成在线；日志里有没有连接超时 | |
| 5 | 按键说话 | 按住 PWR 键说话，松开后有语音回复 | |
| 6 | 显示图片 | 对 Muse 说“在我的小设备上显示一张猫的图片” | |
| 7 | 电池 | 拔掉 USB 后电量显示正常 | |
| 8 | 内存 | 第 4 步日志里 `ready:` 那行的两个数字 | |

**关于第 4 项**：板子自带 VPN（Shadowsocks），打开后只有 Muse 的连接（`*.muse.ai`、`*.metaaivm.com`）走所选节点，其余直连。连不上时看 **设置 › VPN**：节点能否测速、状态里有没有“Muse 正在走 VPN”；日志里搜 `boopie_vpn`。

## 7. 以后怎么更新

- 板子联网约 3 分钟后检查一次，之后每天一次。有新版本时，宠物会说一声，**设置 › 系统更新** 显示版本和更新说明
- 点两下「立即更新」才开始。下载走 VPN（开着的话），签名和每个文件都会校验；装好自动重启，新版本跑不起来会自动退回旧版本
- 同一页可以在正常版和解锁版之间切换，存档不变

## 恢复原厂固件

```
python -m esptool --chip esp32s3 -p COM5 -b 921600 write-flash 0x0 waveshare-175c-factory.bin
```
