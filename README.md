# boopie-firmware

Boopie（布比）：一块圆屏小设备上的多应用系统。Muse 是常驻的 AI 应用，同时支持国内大模型（双大脑），主界面是电子宠物，另有表盘、小游戏和信息卡片。

- 主板：微雪 ESP32-S3-Touch-AMOLED-1.75C
- 第二板：乐鑫 ESP-Mosaico（ESP32-S31）
- 起点：Meta 开源的 [muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk)（Apache 2.0）

## 文档

- [设计方案](docs/design.html)：项目定位、Muse 官方发布与条款、两块硬件、双大脑架构、形象与表情规范、应用规划、阶段路线、风险和参考资料

GitHub 不会直接渲染 HTML，下载后用浏览器打开即可；也可以开启 GitHub Pages，通过网页访问。

## 约定

- Muse SDK token 只存放在仓库 Secrets，名称 `MUSE_SDK_TOKEN`，不写进代码
- 本项目不是 Meta 的产品，也未获得 Meta 背书
