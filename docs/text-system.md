# 中英文字系统

Boopie 的最底层同时支持中文和英文，不管接 Muse 还是国内大脑，屏幕上的回复、字幕和 Wi-Fi 名称都能正常显示中文，排版规则也是同一套。

官方固件只带了 Montserrat 和 unscii 两种拉丁字体，中文会显示成方框，排版也只按空格断行。

## 一、核心约定：按“列”排版

屏幕文字按列排：**英文字母、数字占 1 列，汉字和全角标点占 2 列**（Unicode 的 East Asian Wide / Fullwidth 字符）。

- 规则写在 `components/boopie/boopie_text.c` 的 `WIDE` 表里，这是唯一的来源
- 字体生成器读取同一张表，宽度和规则不符的字形不收录，所以“列数 × 像素”永远等于实际绘制宽度
- 官方的排版代码本来就按列（`M` 的宽度）算布局，换了字体也能自动适配

## 二、字体

### 界面文字：思源黑体（平滑）

设置页、各个页面、输入法、引导、游戏卡片里的中文用**思源黑体**（Noto Sans SC，SIL OFL 1.1），和英文的 Montserrat 同样是平滑字体，风格统一：

- 英文、数字照旧用 Montserrat；Montserrat 没有的中文字，自动回退到同字号的思源黑体（`boopie_font_with_cjk`）
- 字体放在**资源区**（`esp32/assets/fonts/ui.otf`，见[存储设计](boopie-storage.md)），不占程序区；LVGL 的 TinyTTF 直接从 Flash 映射读取，任意字号、抗锯齿，用过的字形缓存 256 个
- 裁剪到 GB2312 全部字符、拉丁字母、中文标点、全角符号和界面用到的 ★☆ 等，共 7812 个字符，**约 1.5 MB**。重新生成：`python3 tools/boopie/gen_ui_font.py --source NotoSansSC-Regular.otf`（下载地址见脚本说明）
- 资源区没有字体或损坏时，自动退回下面的像素字体，板子照常能用

### 字幕和像素画面：像素字体

**回复字幕仍用像素字体**：字幕按“列”分页（见第一节），需要每个字宽度固定；像素字体也和像素风的角色统一。游戏里的分数等是直接画在像素网格上的。

`components/boopie/font/`，两个尺寸共用同一份 12px 点阵：

| 字体 | 大小 | 用途 |
|---|---|---|
| `boopie_font_pixel_24` | 每个点放大成 2×2，行高 24px；英文 12px 宽，汉字 24px 宽 | 回复和字幕（1.75C 上约 2.3 mm 高） |
| `boopie_font_pixel_12` | 1:1，行高 12px | 小屏、密集文字、小字号的回退 |

- 风格和官方的 unscii_16 一致（也是 8px 点阵放大 2 倍），像素风统一
- 只存 1 位点阵，绘制时放大，**20149 个字形约 400 KB**，直接编进固件，跟着 OTA 一起更新
- 自己实现的 LVGL 字体驱动（`boopie_font.c`），不需要文件系统，也不额外占用内存

**收录范围**：全部 GBK 汉字（两万多个，覆盖简繁）、全部 GB2312 字符、ASCII、拉丁字母（含拼音声调 ā á ǎ à）、全角符号、中文标点、①②③、℃、№ 等。GB2312 一级常用字 3755 个全部覆盖（有测试保证）。

**字形来源**（均为 SIL OFL 1.1 开源协议，可商用，许可证随附在 `font/` 目录）：

1. [缝合像素字体](https://github.com/TakWolf/fusion-pixel-font) 对方舟字体的补丁
2. [方舟像素字体](https://github.com/TakWolf/ark-pixel-font) 12px，简体字形优先
3. 方舟缺的 888 个汉字用 [Cubic 11（俐方体 11 号）](https://github.com/ACh-K/Cubic-11) 按 12px 光栅化补齐，对齐方式和缝合像素字体相同
4. 全角破折号“——”自行合成（以上来源都没有）

**还缺的**：GB2312 里 143 个生僻字（如“鼗”“赝”）、数学符号（∑ √ ∞）和制表符。缺字时 LVGL 不画任何东西，不会出现乱码。

**重新生成**：

```sh
git clone --filter=blob:none https://github.com/TakWolf/ark-pixel-font.git
git clone --filter=blob:none https://github.com/TakWolf/fusion-pixel-font.git
pip install pillow fonttools brotli
python3 tools/boopie/gen_pixel_font.py --ark ark-pixel-font --fusion fusion-pixel-font
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
| `muse_ui.c` | 回复和字幕改用 `boopie_font_pixel_24`（`REPLY_FONT`） |
| `muse_chat_text.c` | 分页时汉字算 2 列，加入中文断行和避头尾 |
| `muse_text.[ch]`、`muse_state.c` | 回复字幕保留中文引号和破折号（“” ‘’ ——），不再替换成 ASCII |
| `muse_settings_ui.c` | 设置页标签用 `boopie_font_with_cjk()`：Montserrat 缺的字回退到像素中文字体（20px 及以上用 24px，以下用 12px），Wi-Fi 名称、设备名能显示中文 |

## 五、测试

- `components/boopie/tests/test_boopie_text.py`：列宽、字体与宽度规则逐字一致、常用字覆盖、中文断行、避头尾、中英混排，以及英文行为不变
- 模拟器截图（CI 产物 `boopie-ui-shots-*`）包含纯中文和中英混排两张字幕

## 六、后续

- 高清形象风格可能需要平滑（抗锯齿）的中文字体，到时再加一个矢量字体，放进 Flash 的资源分区
- 状态文字（READY、LISTENING）和设置页的固定文案仍是英文，界面要不要做中文版另外决定
