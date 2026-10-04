# AI 能操控的本地功能（能力清单）

## 怎么分“聊天”和“操控”
不靠关键词。板子把“我会做哪些事”列成一张清单交给 AI（Muse 是开机注册的设备命令，小智是 MCP 工具），你说的话照常送到云端，由 AI 按意思自己决定：只是聊天就直接回答；要动板子就调用对应的工具，板子执行完把结果告诉 AI，AI 再用一句话回你。一句话里聊天和操控可以混着来（“声音大点，再讲个笑话”）。

## 一份清单，两边共用
- 清单：`components/boopie/tools/boopie_tools_spec.c`（名字、说明、参数）。加一项，Muse 和小智同时都有。
  - Muse：`main/noise_control.cpp` 注册时调用 `boopie_tools_describe()`，`main/app.c` 收到命令调用 `boopie_tools_call()`。
  - 小智：`tools/list` 回 `boopie_tools_mcp_json()`，名字前加 `self.`（如 `self.pet.name`）；`tools/call` 在单独的小任务里执行（内部内存栈，可以写 NVS），结果原样回给服务器。
- 执行：`components/boopie/tools/boopie_tools.c`。会改变东西的操作，屏幕下方会弹一个小提示两秒半（“改名啦：豆豆”“已静音”“亮度 80”），一看就知道是板子真做了。
- 清单要小于 8 KB（小智一页发完），`test_boopie_tools.py` 会检查。

## 现有工具
| 名字 | 做什么 | 提示 |
|---|---|---|
| `pet.status` | 宠物名字、饿不饿、心情、等级、经验、星星 | — |
| `pet.name` | 改名（不带参数则报名字） | 改名啦：X |
| `pet.feed` | 喂食（不饿就不吃） | 喂好啦 |
| `display.avatar` | 换角色、颜色、背景、皮肤、配饰、表情、特效 | 换好啦 |
| `game.start` | 开小游戏 | （直接打开游戏） |
| `app.open` | 打开页面：主屏、小窝（可指定房间）、聊天记录、相册、白噪音、设置（可指定子页） | （直接打开） |
| `noise.play` / `noise.stop` | 白噪音、粉红噪音、雨声、海浪，可定时 | 正在播放 |
| `device.sound` | 音量 0–100、静音开关 | 音量 N / 已静音 |
| `device.brightness` | 屏幕亮度 10–100 | 亮度 N |
| `device.battery` | 电量、是否充电、电压 | — |
| `garden.status` | 农场每块地的情况 | — |
| `world.weather` | 把今天的天气同步到小窝 | — |
| `storage.clear` | 清聊天记录/相册/留言：只在屏幕上提问，必须用户点确认 | （屏幕提问） |

不提供重启、恢复出厂这类操作。

## 模拟器
`tool=名字:参数JSON`，如 `tool=pet.name:{"name":"豆豆"}`、`tool=app.open:{"app":"nest","room":"beach"}`（需要先 `idf.py reconfigure` 拉下 cJSON）。
