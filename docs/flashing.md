# 到货操作手册（阶段 1）

微雪 ESP32-S3-Touch-AMOLED-1.75C 到货后按顺序做。全程大约一小时，大半时间在备份。

## 到货前准备

- [ ] 在 [gadgets.muse.ai](https://gadgets.muse.ai/settings/sdk-tokens) 生成 SDK token，存进仓库 **Settings › Secrets and variables › Actions**，名称 `MUSE_SDK_TOKEN`。token 不进代码、不发聊天
- [ ] 存好 Secret 后，到 **Actions › Boopie firmware** 点 **Run workflow** 重新编一次，确认运行摘要里没有 “MUSE_SDK_TOKEN is not set” 警告，下载产物 `boopie-waveshare-s3-175c-<提交号>`，里面的 `BUILD_INFO.txt` 应写着 `token: yes`
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

从 Actions 产物里取 `boopie-waveshare-s3-175c-merged.bin`。第一次刷机先整片擦除，避免原厂固件残留的 NVS 数据和 Muse 固件冲突：

```
python -m esptool --chip esp32s3 -p COM5 -b 921600 erase-flash
python -m esptool --chip esp32s3 -p COM5 -b 921600 write-flash 0x0 boopie-waveshare-s3-175c-merged.bin
```

不想用命令行，也可以用 Chrome / Edge 打开 [esptool-js 网页刷机](https://espressif.github.io/esptool-js/)：Connect → Erase Flash → 地址填 `0x0`、选合并包 → Program。

刷完按一下 PWR 键或拔插 USB 重启。

> 以后再刷新版本**不用擦除**，直接 `write-flash`，配对和 Wi-Fi 会保留。

> **分区表**：Boopie 用自己的 `esp32/partitions_boopie_32mb.csv`，32 MB 全部用上（详见[存储设计](boopie-storage.md)）：两个程序区各 8 MB（官方是 4 MB），加上加密密钥、崩溃记录、10 MB 资源区（字体、提示音、主题）和约 5.7 MB 用户数据区（聊天记录、相册、留言）；设置区等位置和官方一样。分区表只能通过 USB 刷机写入，在线升级改不了，所以第一次刷机就用这张表，以后不用再插线改分区。
>
> 合并包里含资源区，文件约 17 MB，但大部分是空白，压缩后不到 2 MB，esptool 刷写时也会压缩传输，不会慢多少。

## 4. 看启动日志

```
python -m serial.tools.miniterm COM5 115200
```

（按 `Ctrl+]` 退出。）正常启动能看到：

- `link.main: Muse Gadget starting`
- `muse: board: Waveshare ESP32-S3-Touch-AMOLED-1.75C`
- `muse: ready: free heap ... internal, ... psram`

整段日志复制下来存成文本，后面要用。看到 `panic`、`abort` 或反复重启，直接把日志发给 Claude。

## 5. 配对

1. 屏幕上应显示形象，下方暗字显示 `MuseGadget-XXXXXX`。
2. Muse App：**设置 › 设备 › 添加设备**（右上角 **+**），选这个名字。
3. 屏幕提示确认时，按一下板子上的按键。
4. 在 App 里给它配 Wi-Fi。

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

**关于第 4 项**：固件现在**没有代理功能**，板子直连 `api.muse.ai` 和 `hatch.metaaivm.com`。在国内大概率连不上，可以这样试：

- 路由器本身能科学上网（OpenClash 等透明代理）：直接连家里 Wi-Fi；
- 用一台开了全局代理并支持“热点共享代理”的安卓手机开热点给板子连；
- iOS 热点通常不走手机上的 VPN，可以试一下记录结果。

连不上就按设计方案把“可配置代理”提前到阶段 2 的第一项。

## 7. 做自己的形象（可选）

官方脚本 `esp32/tools/muse/avatar.py` 需要板子通过 USB 连着运行它的电脑，并装好 ESP-IDF，目前不适合纯云端流程。手动路线见 `esp32/tools/muse/AVATAR_RECIPE.md`：在 Muse App 里发提示词拿到 `muse_pixel.c`，交给 Claude 接入云端编译（官方把这个文件设为不提交，接入方式到时再定）。

## 恢复原厂固件

```
python -m esptool --chip esp32s3 -p COM5 -b 921600 write-flash 0x0 waveshare-175c-factory.bin
```
