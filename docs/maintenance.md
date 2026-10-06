# 维护手册

怎么编译、测试、发布新版本、跟进上游，以及几条踩过坑才定下来的规矩。用户看的上手说明在 [README](../README.md) 和 [刷机手册](flashing.md)。

## 一、环境

- **ESP-IDF v6.0.1**，别的版本官方不支持。云端用 `espressif/idf:v6.0.1` 镜像
- 目标芯片 `esp32s3`，板子是微雪 ESP32-S3-Touch-AMOLED-1.75C
- 主机测试要 Python 3、C 编译器、`libmbedtls-dev`、NumPy、Pillow
- 模拟器要 CMake、Ninja、SDL2（和 GitHub Actions 里的 `simulator` 任务一样）

## 二、编译

在 `esp32/` 目录下：

```sh
DEFAULTS="sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-waveshare-s3-175c;devices/sdkconfig.boopie-175c"
idf.py -B build-boopie-175c -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG=build-boopie-175c/sdkconfig -DSDKCONFIG_DEFAULTS="$DEFAULTS" build
idf.py -B build-boopie-175c -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG=build-boopie-175c/sdkconfig -DSDKCONFIG_DEFAULTS="$DEFAULTS" merge-bin -o boopie-merged.bin
```

- 合并包写在编译目录里（`build-boopie-175c/boopie-merged.bin`），从 `0x0` 刷入
- 资源包 `boopie_assets.bin`（字体、像素字库、小世界背景、提示音）由 `tools/boopie/pack_assets.py` 在编译时打包，刷在 `assets` 分区
- **解锁版**：配置前设环境变量 `BOOPIE_UNLOCK_ALL=1`，并用单独的编译目录
- **改了 `sdkconfig.defaults` 或 `devices/` 里的配置**：要删掉编译目录里生成的 `sdkconfig`，不然新配置不生效
- **本地编译的固件没有在线更新**：服务器地址和公钥（`CONFIG_BOOPIE_OTA_URL`、`CONFIG_BOOPIE_OTA_PUBKEY`）只在发布流程里注入，设置页会显示「这个版本没有开在线更新」
- `version.txt` 是本地编译的版本号（0.9.0），比任何正式版都低，所以本地刷的板子能收到正式版的更新。正式版的版本号来自 tag

## 三、测试

```sh
cd esp32
python3 -m unittest discover -s components/boopie/tests -p 'test_*.py'   # Boopie 的逻辑，C 代码在电脑上编译后测
python3 -m unittest discover -s tests -p 'test_*.py'                     # 官方的协议和契约测试（先编译一次固件）
node ../cloud/ota-worker/test/worker.test.mjs                            # 更新服务器
```

官方的契约测试会按源码里的写法检查，比如短任务要用 `app_task_spawn` 创建。改了这类写法，要同步改测试。

## 四、模拟器

```sh
cmake -S esp32/simulator -B build/simulator -G Ninja -DMUSE_SIM_WAVESHARE_175C=ON
cmake --build build/simulator
python3 tools/boopie/sim_shots.py --binary build/simulator/muse_simulator --out shots
```

- 模拟器跑的是同一份界面代码，466×466 圆屏
- 截一张图：`muse_simulator --headless --scenario 场景.txt --screenshot out.ppm`
- 场景文件一行一个动作，例如 `settings=update`、`drag=-200`、`advance=400`、`face=idle`；`--help` 列出全部
- 用环境变量模拟状态，例如 `BOOPIE_VPN=1`、`BOOPIE_OTA=found`、`BOOPIE_GUIDE=1`；桩函数在 `esp32/simulator/src/sim_settings.c`

## 五、GitHub Actions

| 工作流 | 什么时候跑 | 做什么 |
|---|---|---|
| `boopie.yml` | 每次推送 | 编译固件、两组主机测试、模拟器截图，产物在 Artifacts |
| `boopie-release.yml` | 打 `v*` tag，或在 Actions 页手动运行 | 编译正常版和解锁版、签名、上传 R2、部署 Worker、建 GitHub Release |
| `esp32.yml`、`linux.yml` | 只能手动 | 官方原有的全板子编译，平时不用 |

## 六、发布新版本

### 1. 一次性准备（已完成）

| GitHub Secret | 内容 | 怎么来 |
|---|---|---|
| `BOOPIE_OTA_KEY` | 签名私钥（P-256 PEM） | 在自己电脑上运行 `python3 tools/boopie/ota_keygen.py` 生成 |
| `CLOUDFLARE_API_TOKEN` | Cloudflare API 令牌 | 用「编辑 Cloudflare Workers」模板创建，确认有 Workers 脚本：编辑、Workers R2 存储：编辑 |
| `CLOUDFLARE_ACCOUNT_ID` | Cloudflare 账号 ID | 后台 Workers 和 Pages 页右侧 |

可选的仓库变量 `BOOPIE_OTA_URL` 用来固定服务器地址。不设时，流程会自动查到 `https://boopie-ota.<账号子域名>.workers.dev/v1`，现在是 `https://boopie-ota.wenjinuu.workers.dev/v1`。

### 2. 每次发布

二选一：

- **Actions › Boopie release › Run workflow**：填版本号（`x.y.z`）和更新说明，换行写 `\n`。流程自己打 tag
- 在本地打附注 tag 并推送：`git tag -a v1.2.0 -m "这次更新：……"`，然后 `git push origin v1.2.0`。tag 说明就是更新说明

规则：
- 版本号必须比上一版大，板子才会当作新版本
- 同一个版本号重跑，会沿用已有的 tag
- 一次发布 15～25 分钟。完成后，Releases 页有两个完整烧录包和分区文件，服务器上的更新清单同步换成新版本

### 3. 发布流程做了什么

1. 用 tag 的版本号编译正常版和解锁版，注入服务器地址和公钥。
2. `tools/boopie/ota_release.py` 生成 `manifest.json`：版本、日期、更新说明，以及两个固件和资源包的大小、SHA-256。
3. 用私钥对清单做 ECDSA P-256 签名，转成 64 字节的 `manifest.sig`。
4. 部署 `cloud/ota-worker`，先上传固件文件，再上传签名和清单。
5. 从外网读一次清单，确认上线，再建 GitHub Release。

### 4. 密钥

- **私钥**只在 `BOOPIE_OTA_KEY` 和你自己的离线备份里。不要提交，不要发到聊天里
- **私钥丢了**：已经刷进旧公钥的板子再也收不到在线更新，只能用 USB 重刷一次带新公钥的固件。换密钥的步骤一样：生成新密钥，更新 Secret，发布，然后每块板子用 USB 刷一次
- **Cloudflare 令牌泄露**：在 Cloudflare 里轮转令牌，再更新 Secret。旧令牌没法伪造更新，签名在私钥那边

## 七、板子上的在线更新

细节见 [OTA 设计](boopie-ota.md)。维护时要记住：

- 板子只认带签名的清单，并逐个校验文件的 SHA-256
- 新固件写进另一个程序区。启动后界面要正常跑 45 秒，Wi-Fi 连上（或者没设 Wi-Fi），才算装好；否则 bootloader 退回旧版本
- 资源包先下载到用户数据区，下次以对应版本启动时才写进 `assets` 分区，头部最后写
- Muse 官方推送的升级（蓝牙、配网、控制通道）一律拒收：`CONFIG_HOMEHUB_OTA_ENABLED=n`

## 八、跟进上游

```sh
git remote add upstream https://github.com/facebookincubator/muse-gadget-sdk.git
git fetch upstream main
git merge upstream/main
```

- 冲突多半在 Muse 官方代码里标了 `Boopie:` 的地方
- 合并后跑一遍编译、两组测试和模拟器
- 只想要上游某个修复时，用 `git cherry-pick`，冲突时优先用上游的写法
- 遇到 Muse 行为问题，先去读上游的提交和代码，不要猜（例如回复模式必须是文字，见 `muse_chat_priv.h`）

## 九、规矩（踩过的坑）

### 内存

- **内部 RAM 很紧**：短任务用 `app_task_spawn`，栈放 PSRAM
- **PSRAM 栈的任务不能映射 Flash**：`esp_partition_mmap`、`esp_ota_*` 这类会冻结缓存，会让 PSRAM 栈断言崩溃。这类调用放到内部 RAM 栈的小任务里做，例如 OTA 的 `set_boot_task`
- **不要映射 Flash 读资源**：用 `esp_partition_read`。16 MB 以上的 Flash 映射不了，读的时候还可能遇上正在写入
- **LVGL 样式里不要用 `opa`、`transform`、`rotation`**：它们会建 ARGB 图层，PSRAM 不够时会一直重试，界面就卡死了。要变暗就用黑色遮罩子对象

### 字体

- 平滑中文（`ui.otf`）在资源分区里，经 `lv_fs` 驱动 `B:` 按块读
- 两个绘制线程共用一把递归锁

### 安全和隐私

- 固件里不带任何 token，日志里不打印 token 和订阅内容
- 不关 TLS 校验。VPN 只按 SNI 转发，证书照常端到端校验
- NVS 没有加密，这是有意的取舍。拿到板子的人能读出 token，所以必要时去 gadgets.muse.ai 吊销

## 十、排查问题

- **崩溃**：下次开机，串口会打印上次崩溃的原因和调用栈（`addr2line` 格式），然后清掉 coredump。用同一次编译的 `muse-gadget.elf` 解析
- **串口日志**：`python -m serial.tools.miniterm COM5 115200`
- **常用日志标签**：
  - `boopie.ota`：在线更新
  - `boopie_vpn`：VPN
  - `muse_chat_session`：Muse 对话
  - `boopie_xz`：小智
- **内存**：开机日志里 `ready: free heap ... internal, ... psram` 那一行

## 十一、文档索引

| 文档 | 内容 |
|---|---|
| [设计方案](design.html) | 定位、条款、硬件、架构、路线和现状 |
| [交互设计](boopie-interaction.md) | 引导、按键、四向界面、输入法、VPN、小游戏 |
| [形象设定](boopie-character.md) | 角色、表情、宠物养成、皮肤、成就 |
| [小世界](boopie-world.md) | 屋里、户外、森林、海边、商店 |
| [能力清单](boopie-tools.md) | AI 能操控的本地功能 |
| [小智接入](boopie-xiaozhi.md) | 协议和步骤 |
| [存储设计](boopie-storage.md) | 分区、资源区、用户数据区 |
| [中英文字系统](text-system.md) | 字体、排版、断行 |
| [OTA 设计](boopie-ota.md) | 在线更新 |
| [刷机手册](flashing.md) | 备份、刷机、配对、验证 |
| [代码摸底说明](code-survey.md) | 官方固件结构与挂钩点 |
| [开源协议与第三方](open-source.md) | 协议、署名、条款 |
