# 参与 Boopie

欢迎提 issue 和 pull request。

## 提 issue

- **报 bug**：写清板子型号、固件版本（**设置 › 系统更新** 顶部那行）、怎么复现。能附上串口日志最好；固件崩溃后，下次开机会打印上次崩溃的原因和调用栈，一起贴上
- **贴日志前先看一眼**：去掉 Wi-Fi 密码、token、订阅链接和节点地址。Boopie 自己不会把这些打进日志，但别的组件不一定
- **安全问题**（例如能绕过更新签名）：请不要公开发 issue，先通过 GitHub 私信联系作者

## 提 pull request

1. 从 `main` 拉分支。
2. 改动尽量落在 `esp32/components/boopie/` 里。必须动 Muse 官方代码时，改动越小越好，并在旁边注明 `Boopie:`，方便以后合并上游。
3. 跑一遍检查，见 [维护手册](docs/maintenance.md)：
   - 固件能编译，`check_sizes` 不超过 6 MB 的程序区；
   - 主机测试通过：`esp32/components/boopie/tests` 和 `esp32/tests`；
   - 改了界面的话，在模拟器里截图看过。
4. 新文件加上 Apache-2.0 文件头（见 [开源协议与第三方](docs/open-source.md)）。
5. 用户看得到的文字用简体中文，代码注释和提交说明用英文，跟现有代码一致。

## 不收的东西

- 任何密钥、token、订阅链接、私钥，哪怕是测试用的
- 来源不明或协议不允许再发布的素材：图片、字体、音效
- 新的品牌角色和二创形象。现有的只供个人使用，见 [开源协议与第三方](docs/open-source.md)

## 协议

提交的代码按仓库的 [Apache License 2.0](LICENSE) 发布。不需要签 Meta 的 CLA，这个仓库不是 Meta 的项目。想把改动贡献回官方 [muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk)，请按官方仓库的流程走。

社区行为准则见 [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)。
