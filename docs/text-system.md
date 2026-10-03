# 中英文字系统

Boopie 的最底层同时支持中文和英文，不管接 Muse 还是国内大脑，屏幕上的回复、字幕和 Wi-Fi 名称都能正常显示中文，排版规则也是同一套。

官方固件只带了 Montserrat 和 unscii 两种拉丁字体，中文会显示成方框，排版也只按空格断行。

## 一、核心约定：按“列”排版，按实际字宽断行

屏幕文字按列排：**英文字母、数字占 1 列，汉字和全角标点占 2 列**（Unicode 的 East Asian Wide / Fullwidth 字符）。

- 规则写在 `components/boopie/boopie_text.c` 的 `WIDE` 表里，这是唯一的来源
- 官方的排版代码按列算布局（字幕框多宽、几行），这一点不变
- 字体是比例字体（i 窄、W 宽），所以回复分页时**按每个字的实际宽度断行**：一列等于半个汉字宽（22px 字号下 11px），一行 N 列就是 N × 11px，每个字量出实际像素宽度往里放（`boopie_text_set_measure`）。量宽度用的是从字体里导出的字宽表（`font/boopie_ui_metrics.c`），取整方式和 LVGL 的 TinyTTF 完全一样，不依赖 LVGL，语音任务里也能用

## 二、字体：思源黑体（已定，全部替换像素字体）

屏幕上所有中文，包括设置页、各个页面、输入法、引导、游戏卡片，以及**回复和字幕**，都用**思源黑体**（Noto Sans SC，SIL OFL 1.1），平滑抗锯齿：

- 字幕和回复：思源黑体 22px，中英文都用它（`REPLY_FONT`）
- 其他界面：英文、数字照旧用 Montserrat，Montserrat 没有的中文字自动回退到同字号的思源黑体（`boopie_font_with_cjk`）
- **编进固件**（`components/boopie/font/ui.otf`，约 1.5 MB），跟着 OTA 更新，不依赖资源区；LVGL 的 TinyTTF 直接从 Flash 读，任意字号，用过的字形每个字号缓存 256 个
- 裁剪到 **GB2312 全部字符**（含一二级汉字 6763 个）、拉丁字母（含拼音声调）、中文标点、全角符号、①②③、℃ 和界面用到的 ★☆ 等，共 7812 个字符。GB2312 一级常用字 3755 个全部覆盖（有测试保证）
- **GB2312 以外的生僻字和繁体字**显示成 LVGL 的占位方框。全部 GBK 要 5 MB，程序分区放不下，这是有意的取舍
- 原来的像素中文字体（约 400 KB）已删除，固件净增约 1.1 MB。状态行（SPEAKING、BATTERY）和设置页标题仍是官方的英文像素字体 unscii，只有英文

**重新生成**（字体和字宽表一起生成）：

```sh
curl -LO https://raw.githubusercontent.com/notofonts/noto-cjk/main/Sans/SubsetOTF/SC/NotoSansSC-Regular.otf
pip install fonttools
python3 tools/boopie/gen_ui_font.py --source NotoSansSC-Regular.otf
```

## 三、断行规则

回复分页（`muse_chat_text.c` 的 `next_line`）：

- 英文：和原来一样，只在空格处断行，单词太长才拆
- 中文：任意两个汉字之间都可以断行
- 中英混排：汉字和英文单词之间可以断行
- **避头尾**：句末标点（，。！？、；：）」》等）不出现在行首，开头标点（（「《“等）不出现在行尾，需要时把前一个字一起挪到下一行

字幕标签用 LVGL 自带的换行，它本来就支持中文逐字断行。

## 四、接进官方代码的位置

| 位置 | 改动 |
|---|---|
| `muse_ui.c` | 回复和字幕改用 22px 思源黑体（`REPLY_FONT`），并把它的字宽交给分页 |
| `muse_chat_text.c` | 分页时按实际字宽断行（没设置时汉字算 2 列），加入中文断行和避头尾 |
| `muse_text.[ch]`、`muse_state.c` | 回复字幕保留中文引号和破折号（“” ‘’ ——），不再替换成 ASCII |
| `muse_settings_ui.c` | 设置页标签用 `boopie_font_with_cjk()`：Montserrat 缺的字回退到同字号的思源黑体，Wi-Fi 名称、设备名能显示中文 |

## 五、测试

- `components/boopie/tests/test_boopie_text.py`：列宽、字宽表和字体文件逐字一致（用 fontTools 核对）、常用字覆盖、按实际字宽分页每行不超宽、中文断行、避头尾、中英混排，以及英文行为不变
- 模拟器截图（CI 产物 `boopie-ui-shots-*`）包含纯中文和中英混排两张字幕

## 六、后续

- 状态文字（READY、LISTENING）和设置页的固定文案仍是英文，界面要不要做中文版另外决定
