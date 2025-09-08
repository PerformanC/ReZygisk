# ReZygisk

[English](../README.md)

ReZygisk 是 Zygisk Next 的一个分支，一个独立的 Zygisk 实现，为 KernelSU、APatch 和 Magisk（官方版和 Kitsune 版）提供 Zygisk API 支持。

它旨在将代码库完全现代化并用 C 语言重写，从而以更宽松、更对开源社区友好的许可证，实现一个更高效、更快速的 Zygisk API。

## 为什么选择 ReZygisk？

Zygisk Next 的最新版本不是开源的，其代码仅供其开发者使用。这不仅限制了我们为该项目做出贡献的能力，也使得代码审计变得不可能，这是一个主要的安全问题，因为 Zygisk Next 是一个以超级用户（root）权限运行的模块，可以访问整个系统。

Zygisk Next 的开发者在安卓社区中很有名望且值得信赖，但这并不意味着代码没有恶意或漏洞。我们 (PerformanC) 理解他们将代码闭源的原因，但我们坚信开源才是正确的选择。

## 优势

-   永久开源 (FOSS)

## 依赖

| 工具          | 描述               |
|---------------|-------------------|
| `Android NDK` | 安卓原生开发工具包 |

### C++ 依赖

| 依赖    | 描述                      |
|---------|--------------------------|
| `lsplt` | 一个简单的安卓 PLT Hook 库 |

## 安装

### 1. 选择正确的 zip 包

选择合适的构建版本/zip 包很重要，因为它将决定 ReZygisk 的隐藏性和稳定性。不过，这并不难：

-   `release` 版本是大多数情况下的首选，它移除了应用级别的日志记录，并提供了更优化的二进制文件。
-   `debug` 版本则相反，它会产生大量的日志并且没有进行优化。因此，**您只应在调试或为提交Issue获取日志时使用它**。

至于分支，您应该始终使用 `main` 分支，除非开发者另有说明，或者您想测试即将推出的功能并意识到其中涉及的风险。

### 2. 刷入 zip 包

选择合适的构建版本后，您应该使用当前的 root 管理器（如 Magisk 或 KernelSU）刷入它。您可以在 root 管理器的 `模块` 部分执行此操作，选择您下载的 zip 包即可。

刷入后，请检查安装日志以确保没有错误，如果一切正常，您可以重启您的设备。

> [!警告]
> Magisk 用户应禁用内置的 Zygisk，因为它会与 ReZygisk 冲突。这可以在 Magisk 的 `设置` 中禁用 `Zygisk` 选项来完成。

### 3. 验证安装

重启后，您可以通过检查 root 管理器 `模块` 部分中的模块描述来验证 ReZygisk 是否正常工作。描述应表明必要的守护进程正在运行。例如，如果您的环境同时支持 64 位和 32 位，它应该看起来类似于这样：`[monitor: 😋 tracing, zygote64: 😋 injected, daemon64: 😋 running (...) zygote32: 😋 injected, daemon32: 😋 running (...)] Standalone implementation of Zygisk.`

## 翻译

目前有两种不同的方式为 ReZygisk 贡献翻译：

-   对于 README 的翻译，您可以在 `READMEs` 文件夹中创建一个新文件，遵循 `README_<语言代码>.md` 的命名约定，其中 `<语言代码>` 是语言代码（例如，巴西葡萄牙语为 `README_pt-BR.md`），然后向 `main` 分支提交一个包含您更改的 pull request。
-   对于 ReZygisk WebUI 的翻译，您应该首先在我们的 [Crowdin](https://crowdin.com/project/rezygisk) 上做出贡献。获得批准后，从那里获取 `.json` 文件，并提交一个包含您更改的 pull request —— 将 `.json` 文件添加到 `webroot/lang` 文件夹，并将您的名字按字母顺序添加到 `TRANSLATOR.md` 文件中。

## 支持

有关 ReZygisk 或其他 PerformanC 项目的任何问题，请随时加入以下任一渠道：

-   Discord 频道: [PerformanC](https://discord.gg/uPveNfTuCJ)
-   ReZygisk Telegram 频道: [@rezygisk](https://t.me/rezygisk)
-   PerformanC Telegram 频道: [@performancorg](https://t.me/performancorg)
-   PerformanC Signal 群组: [@performanc](https://signal.group/#CjQKID3SS8N5y4lXj3VjjGxVJnzNsTIuaYZjj3i8UhipAS0gEhAedxPjT5WjbOs6FUuXptcT)

## 贡献

为 ReZygisk 做出贡献，必须遵守 PerformanC 的[贡献指南](https://github.com/PerformanC/contributing)。请遵循其安全策略、行为准则和语法标准。

## 许可证

ReZygisk 主要在 GPL 许可下授权，由 Dr-TSNG 提供，但由 The PerformanC Organization 重写的代码则在 AGPL 3.0 许可下授权。您可以在[开源促进会](https://opensource.org/licenses/AGPL-3.0)上阅读更多相关信息。