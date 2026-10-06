# 在线更新（OTA）

Boopie 从自己的服务器更新：Cloudflare R2 存文件，前面一个 Worker 提供下载（`cloud/ota-worker`）。Muse 官方推送的升级（蓝牙、配网、控制通道里的 `device.ota`）一律拒收：`CONFIG_HOMEHUB_OTA_ENABLED=n`。

发布怎么做，见 [维护手册 › 发布新版本](maintenance.md#六发布新版本)。

## 服务器

| 地址 | 内容 |
|---|---|
| `GET /v1/manifest.json` | 最新版本的清单：版本、日期、更新说明，两个固件和资源包的文件名、大小、SHA-256 |
| `GET /v1/manifest.sig` | 清单的 ECDSA P-256 签名，64 字节（r ‖ s） |
| `GET /v1/files/<文件名>` | 固件或资源包；支持 Range，断了能续传 |

现在的地址：`https://boopie-ota.wenjinuu.workers.dev/v1`。清单不缓存；文件名里带版本号或哈希，可以永久缓存。

服务器本身不被信任：清单要用只在发布流程里的私钥签名，每个文件要和清单里的 SHA-256 一致。服务器被人改了，板子也只会拒装。

## 清单

```json
{
 "schema": 1,
 "board": "waveshare-s3-175c",
 "version": "1.0.0",
 "date": "2026-10-06",
 "notes": "这次更新的说明",
 "app": { "file": "boopie-1.0.0.bin", "size": 2953216, "sha256": "…" },
 "app_unlocked": { "file": "boopie-1.0.0-unlocked.bin", "size": 2953216, "sha256": "…" },
 "assets": { "file": "assets-3-69fbfdde.bin", "size": 2265664, "sha256": "…", "version": 3 }
}
```

## 板子上（`components/boopie/ota`）

**检查**

- Wi-Fi 连上约 3 分钟后检查一次，之后每天一次；在 **设置 › 系统更新** 可以随时点「检查更新」
- 读清单和签名，用编进固件的公钥（`CONFIG_BOOPIE_OTA_PUBKEY`）验签，再核对 `board`
- 版本号比自己新，就提示有新版本：宠物在主屏说一句，设置首页的「系统更新」后面显示「有新版本」

**安装**（点两下「立即更新」才开始）

1. 资源包和板子上的不同（按 SHA-256 比较），先下载到用户数据区 `/data/ota_assets.bin`，同时记下它属于哪个版本。
2. 固件直接写进另一个程序区，边下边算 SHA-256；断了用 Range 续传，最多重试 6 次。
3. 校验通过后，用内部 RAM 栈的小任务设置启动分区。这一步要映射 Flash，PSRAM 栈的任务不能做。
4. 重启。新固件第一次启动时，在读任何资源之前，把下载好的资源包写进 `assets` 分区，头部最后写，写一半断电不会被当成有效包。

**正常版和解锁版**

- 每次发布都有两种固件：正常版和解锁版（`BOOPIE_UNLOCK_ALL`，皮肤、配饰、颜色、背景全开）
- 板子只跟着自己这种更新，不会自己换成另一种
- 页面上有「换成解锁版」「换成正常版」，同样点两下才开始，同样验签、校验
- 存档不变：换回正常版后，用星星买过的都还在，没买过的重新锁上

**保护**

- **自动退回**：新固件启动后，界面正常跑 45 秒，并且 Wi-Fi 连上（或者没设 Wi-Fi），才算装好；否则 bootloader 退回旧版本。以前这个判断依赖 Muse 的控制连接，用小智的人会被误退回，所以改了
- **资源包只给对应的固件**：资源包和固件版本绑定。新固件没装成功，或者退回了旧版本，下载好的资源包不会写进去
- **资源包坏了自动修**：检查时发现资源包无效，不用确认，直接重新下载
- **走 VPN**：VPN 开着时，更新服务器的域名也走 VPN；TLS 证书照常端到端校验

## 文件

| 文件 | 作用 |
|---|---|
| `esp32/components/boopie/ota/boopie_ota.c` | 检查、下载、校验、安装、启动时写资源包、健康检查 |
| `esp32/components/muse/muse_settings_ui.c` | 「系统更新」页 |
| `cloud/ota-worker/` | 服务器 |
| `tools/boopie/ota_release.py` | 生成清单；`rawsig` 把 DER 签名转成 64 字节 |
| `tools/boopie/ota_keygen.py` | 生成签名密钥（纯 Python，不需要装别的包） |
| `.github/workflows/boopie-release.yml` | 发布流程 |
