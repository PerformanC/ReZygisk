# ReZygisk

[English](https://github.com/PerformanC/ReZygisk/blob/main/README.md)

ReZygisk 是 Zygisk 的另一个独立实现，从 Zygisk Next 分叉而来，为 KernelSU、Magisk 和 APatch 提供 Zygisk API 支持(目前处于测试阶段)。

项目致力于使用 C 语言重写原本的 C++ 和 Rust 代码，从而更加现代、高效地实现 Zygisk API，并使用更宽松的开源许可证。

> [!NOTE]
> 此模块还在测试阶段，请仅安装正式版本的压缩包。
>
> 您可以从 [Actions](https://github.com/PerformanC/ReZygisk/actions) 页面下载自动构建包，但要注意自负风险。使用不稳定的版本时，设备可能会陷入启动循环(Bootloop)。

## 为什么要选择 ReZygisk？

最新版本的 Zygisk Next 并不开源，仅其核心开发者有权查阅全部源代码。这不仅阻止了其他开发者贡献代码，还阻止了他人对项目代码进行审计。Zygisk Next 是一个以超级用户(root)权限运行的模块，可以访问整个系统，闭源后存在重大安全隐患。

Zygisk Next 的开发者们在Android社区享有盛誉，备受信任。但这并不意味着他们的项目就一定没有任何恶意代码和漏洞。我们(PerformanC)理解他们出于某些原因不愿保持开源，但我们坚信，开源是更好的选择。

## 优点

- 永远保持开源

## 依赖

| 工具          | 简介                                |
|---------------|------------------------------------|
| `Android NDK` | Android 本地开发工具包               |

### C++ 依赖

| 依赖    | 简介                         |
|---------|-----------------------------|
| `lsplt` | Android 程序链接表钩子        |

## 用法

目前正在编写中 (敬请期待)

## 安装

目前还没有发布正式版本 (敬请期待)

## 翻译

您可以向 [add/new-webui](https://github.com/PerformanC/ReZygisk/tree/add/new-webui) 分支贡献翻译。

请不要忘记在 [TRANSLATOR.md](https://github.com/PerformanC/ReZygisk/blob/add/new-webui/TRANSLATOR.md) 中添加您的 GitHub 账号信息，以便人们看到您的贡献。

## 支持

如果您有任何关于 ReZygisk 或者其他 PerformanC 项目的问题，可以随时加入以下群组：

- Discord 服务器: [PerformanC](https://discord.gg/uPveNfTuCJ)
- ReZygisk Telegram 群组: [@rezygisk](https://t.me/rezygisk)
- PerformanC Telegram 群组: [@performancorg](https://t.me/performancorg)

## 贡献

要对 ReZygisk 做出贡献，请遵守 PerformanC 的 [贡献指南](https://github.com/PerformanC/contributing) 。

请遵循其安全政策、行为准则和语法标准。

## 开源许可证

ReZygisk 项目中，旧的 Zygisk Next 部分采用 GPL 许可证，但由 PerformanC 组织重写的代码则采用 AGPL 3.0 许可证。

您可以在 [Open Source Initiative](https://opensource.org/licenses/AGPL-3.0) 上阅读更多相关信息。
