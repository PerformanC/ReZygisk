# ReZygisk

[English](/README.md)

ReZygisk 是 Zygisk Next 的一个分支，作为 Zygisk 的独立实现，为 KernelSU、APatch 和 Magisk（官方和 Kitsune）提供 Zygisk API 支持。

它旨在现代化并使用 C 完全重写代码库，从而允许以更宽松、更 FOSS 友好的许可证更高效、更快地实现 Zygisk 应用程序编程接口（API）。

## 为什么？

Zygisk Next 的最新版本并非开源，其代码完全被开发者保留。这不仅限制了我们对项目的贡献，也使得代码审计变得不可能。这是一个重大的安全隐患，因为 Zygisk Next 是一个以超级用户（root）权限运行的模块，拥有访问整个系统的权限。

Zygisk Next 的开发者在 Android 社​​区中享有盛誉且备受信赖，但这并不意味着其代码并非恶意的或不具有漏洞。我们（PerformanC）对他们出于某些原因保持代码闭源表示理解，但相较于闭源模块，我们更倾向于开源模块。

## 优点

- FOSS（永久）

## 依赖

| 工具　　　     | 描述　　　　　　　　　|
|---------------|---------------------|
| `Android NDK` | Android 原生开发套件 |

### C++ 依赖

| 依赖项　　         | 描述　　　　　　　　　　　　　　　       |
|-------------------|---------------------------------------|
| `lsplt`　　　　　　| 适用于 Android 的简单 PLT Hook　　　　　|

## 安装

### 1. 选择正确的 ZIP 文件

选择构建/压缩包非常重要，因为它将决定 ReZygisk 的隐蔽性和稳定性。不过，这并不是一项艰巨的任务：

- `release` 应当是大多数情况下的选择，它删除了应用程序级日志并提供更优化的二进制文件。
- `debug`，相较于 `release`，则提供了相反的功能，拥有详尽的日志记录且没有优化，因此，**您应该只将其用于调试目的**和**获取用于创建问题的日志**时。

对于分支，您应该始终使用 `main` 分支，除非开发人员另有说明，或者您想要测试即将推出的功能并知晓所涉及的风险。

### 2. 刷入 ZIP

在选择了正确的版本之后，您应该通过您当前正在使用的 root 管理器（如 Magisk 或 KernelSU）进行刷入————您可以前往 root 管理器的 `模块` 部分并选择您下载的 ZIP 文件进行刷入。

刷入后，请检查安装日志以确保没有错误。如果一切正常，则可以重新启动设备。

> [!WARNING]
> 由于 Magisk 内置的 Zygisk 会与 ReZygisk 冲突，Magisk 用户应禁用内置的 Zygisk——您可以前往 Magisk 的`设置`页面禁用 `Zygisk` 选项。

### 3. 检查安装是否成功

设备重启后，您可以通过检查 root 管理器`模块`部分中的模块描述来验证 ReZygisk 是否正常工作，该描述应该显示必要的守护进程正在运行。例如，如果您的环境同时支持 64 位和 32 位，则应该类似于以下内容：`[monitor: 😋 tracing, zygote64: 😋 injected, daemon64: 😋 running (...) zygote32: 😋 injected, daemon32: 😋 running (...)] Standalone implementation of Zygisk.`

## 翻译

目前有以下两种不同的方式可以为 ReZygisk 贡献翻译：

- 对于 README 的翻译，您可以在 `READMEs` 文件夹中创建一个新文件，遵循 `README_<language>.md` 的命名约定，其中 `<language>` 是语言代码（例如 `README_pt-BR.md` 代表巴西葡萄牙语），随后，向 `main` 分支打开一个包含您的更改的拉取请求（Pull Request，简称 PR）。
- 对于 ReZygisk WebUI 的翻译，您应该首先为我们的 [Crowdin](https://crowdin.com/project/rezygisk) 做出贡献。一旦获得批准，请从那里检索 `.json` 文件并打开包含您更改的拉取请求——将 `.json` 文件添加到 `webroot/lang` 文件夹，并将您的信用按字母顺序添加到 `TRANSLATOR.md` 文件中。

## 支持

如有任何与 ReZygisk 或其他 PerformanC 项目相关的问题，欢迎加入以下任何频道：

- Discord 频道：[PerformanC](https://discord.gg/uPveNfTuCJ)
- ReZygisk 的 Telegram 频道：[@rezygisk](https://t.me/rezygisk)
- PerformanC 的 Telegram 频道：[@performancorg](https://t.me/performancorg)
- PerformanC 的 Signal 群聊：[@performanc](https://signal.group/#CjQKID3SS8N5y4lXj3VjjGxVJnzNsTIuaYZjj3i8UhipAS0gEhAedxPjT5WjbOs6FUuXptcT)

## 贡献

要为 ReZygisk 提供贡献，遵从 PerformanC 的 [贡献守则](https://github.com/PerformanC/contributing) 是必要的。请遵循其安全政策、行为准则和语法标准。

## 使用许可

ReZygisk 主要由 Dr-TSNG 以 GPL 许可证授权，但其重写代码也由 PerformanC 组织以 AGPL 3.0 许可证授权，您可以在[开放源代码促进会](https://opensource.org/licenses/AGPL-3.0)阅读更多相关信息。
