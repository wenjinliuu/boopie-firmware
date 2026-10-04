# 接入小智（官方服务器）

参考：[78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)（MIT 许可，2026-10-03 的 `0d576d3`）及其 `docs/websocket_zh.md`、`docs/mcp-protocol_zh.md`、`main/ota.cc`。

## 现状
- **步骤 1（激活和绑定）已做**：`components/boopie/xiaozhi/`（`boopie_xz_proto.c` 协议、`boopie_xiaozhi.c` 板上任务），编进 muse 组件，开机在 `muse_app_run` 里启动。
  - AI 助手选了小智且 Wi-Fi 连上：向官方接口报到。
  - 没绑定：屏幕说一次“小智激活码 xxxxxx”，设置 › AI 助手 › 小智接入 页大字显示激活码，每 3 秒问一次是否绑好；码过期自动换新码。
  - 绑好：提示“已连接小智！”，对话地址和令牌存 NVS（命名空间 `xiaozhi`），每 6 小时重新报到一次换新令牌。
  - 服务器的 `firmware` 只记一笔日志，从不升级；服务器时间在 NTP 对时前先拿来用。
  - 连不上：10 秒起翻倍重试，最长 5 分钟；设置页有“重新连接”按钮。
  - 按住说话时，如果还没绑定，屏幕会提示激活码。
  - 测试：`test_boopie_xiaozhi.py`（报到内容、UUID、各种回复的解析）。
- **步骤 2（语音对话）已写好，待上板验证**：`boopie_xz_voice.c`，和 Muse 的对话接口一模一样（`muse_voice.c` 里按选中的 AI 助手转给 Muse 或小智，一次对话从头到尾归同一个）。
  - 第一次按下才连 WebSocket，90 秒没说话就断开；连接中录的音先编码存着（约 10 秒），等服务器 hello 后补发。
  - 上行：16 kHz、60 ms 一帧 Opus（参数同官方）；下行：Opus 直接解码成 16 kHz，不用重采样。
  - 字幕：识别出的话（stt）→ 每句回复（sentence_start），按播放进度切换；回复的情绪（llm emotion）说完后让宠物开心/难过/犯困/惊讶几秒。
  - 再按一下打断：发 `abort`，丢掉还没播的回复。
  - 出错提示：连不上（10 秒没 hello）、没听清（松开 10 秒没回应）、没有回应（20 秒没动静）、断开。
  - MCP：回 `initialize`；服务器的 `system` 等指令一律不执行。
- **步骤 3（MCP 工具）已写好，待上板验证**：和 Muse 共用一份能力清单，见 [AI 能操控的本地功能](boopie-tools.md)。
  - 选小智时不存断网留言（Muse 的留言照旧等 Muse）。
  - Opus 放在单独的任务里，24 KB 栈放 PSRAM；WebSocket 任务 6 KB 内部内存。模拟器用 `BOOPIE_XZ_CODE=123456` / `BOOPIE_XZ_READY=1` 看设置页。
- 官方仓库**正式支持我们这块板**（`waveshare/esp32-s3-touch-amoled-1.75`，含 1.75C 变体），也用 ESP-IDF 6.0.1，音频芯片同样是 ES8311（喇叭）+ ES7210（麦克风，带回采），官方配置 24 kHz。
- 从开发环境能访问官方接口 `api.tenclass.net` 和乐鑫组件库（Opus 编解码组件要从这里下载）。

## 服务
- 官方控制台 [xiaozhi.me](https://xiaozhi.me)：注册账号，个人用户可以免费使用千问（Qwen）实时模型；声音复刻等高级功能可能收费或有期限。用户不用填任何 API key。
- 绑定：设备显示 6 位激活码，在控制台“添加设备”里填入。
- 角色、音色、模型、提示词都在控制台的“智能体”里设。

## 协议要点
1. **检查/激活**：`POST https://api.tenclass.net/xiaozhi/ota/`
   - 请求头：`Device-Id`（网卡 MAC）、`Client-Id`（设备自己生成并存在 NVS 的 UUID）、`Activation-Version: 1`、`User-Agent`、`Accept-Language: zh-CN`。
   - 请求体：设备信息 JSON（芯片、闪存、应用名和版本、板子）。
   - 回复里可能有：
     - `activation`（`code` 激活码、`message`、`challenge`、`timeout_ms`）：没绑定时才有。
     - `websocket`（`url`、`token`）：对话地址和令牌，存起来。
     - `server_time`：可顺便对时。
     - `firmware`：官方固件的升级信息。
   - 等绑定：带着 `challenge` 反复 `POST .../ota/activate`，返回 202 表示还没绑好，200 表示绑好。我们没有 eFuse 序列号，请求体发 `{}` 即可。
2. **对话**：WebSocket 连 `url`。
   - 请求头：`Authorization: Bearer <token>`、`Protocol-Version: 1`、`Device-Id`、`Client-Id`。
   - 设备发 `hello`：`features.mcp=true`，音频为 Opus、16 kHz、单声道、60 ms 一帧。
   - 服务器回 `hello`：带 `session_id`，下行音频一般是 24 kHz 的 Opus。
   - 按住说话：发 `{"type":"listen","state":"start","mode":"manual"}`，然后发二进制 Opus 帧；松开发 `state:"stop"`。
   - 服务器回：
     - `stt`：识别出的文字。
     - `llm`：带 `emotion`，用来驱动表情。
     - `tts`：`start` / `sentence_start`（附文字）/ `stop`，中间是二进制 Opus 语音。
   - 打断说话：发 `{"type":"abort"}`。
3. **MCP 工具**：`{"type":"mcp","payload":<JSON-RPC 2.0>}`。服务器依次发 `initialize`、`tools/list`、`tools/call`，工具名形如 `self.xxx`。报上去的是和 Muse 共用的能力清单（[boopie-tools.md](boopie-tools.md)）。

## 必须注意
- **不能自动升级**：回复里的 `firmware` 是官方小智固件，照做会把布比固件整个覆盖掉。我们只读激活码、`websocket` 和 `server_time`，`firmware` 一律忽略。应用名报我们自己的（`boopie`），不报官方板子名。
- Opus 用乐鑫的 `espressif/esp_audio_codec`（官方固件同款），上行在设备上编码，下行直接解码成 16 kHz。
- 按住说话用 `manual` 模式，不开唤醒词，省电；唤醒词以后再说。
- 令牌、`Client-Id` 存 NVS，不写进代码、不打日志。

## 步骤
1. 激活和绑定：开机检查，显示激活码，轮询直到绑定，提示“已连接小智”；顺便对时。
2. 语音对话：Opus、WebSocket、按住说话、播放、字幕、表情。
3. MCP 工具。
4. （可选）唤醒词。

## 用户可以提前做
- 在 xiaozhi.me 注册账号。
- 新建一个智能体：名字叫布比，选好音色，写好人设提示词。
- （可选，验证硬件）先用官方固件试一次：官方支持这块板，能确认麦克风、喇叭和账号都没问题。注意这会覆盖板子上的布比固件和数据，之后再刷回来。
