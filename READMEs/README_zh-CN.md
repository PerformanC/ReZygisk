# ReZygisk

[Bahasa Indonesia](/READMEs/README_id-ID.md)|[Tiếng Việt](/READMEs/README_vi-VN.md)|[Português Brasileiro](/READMEs/README_pt-BR.md)|[French](/READMEs/README_fr-FR.md)|[日本語](/READMEs/README_ja-JP.md)|[简体中文](/READMEs/README_zh-CN.md)

ReZygisk 是 Zygisk Next 的一个分支，Zygisk Next 是 Zygisk 的独立实现，它为 KernelSU、APatch 和 Magisk（官方版和 Kitsune 版）提供 Zygisk API 支持。

它旨在将代码库现代化并完全重写为 C，从而允许使用更宽松且对 FOSS 友好的许可证更高效、更快速地实现 Zygisk API。

## 为什么？

最新版本的 Zygisk Next 不是开源的，完全为其开发人员保留代码。这不仅限制了我们为项目做出贡献的能力，而且无法对代码进行审计，这是一个主要的安全问题，因为 Zygisk Next 是一个以超级用户（root）权限运行的模块，可以访问整个系统。

Zygisk Next 开发人员在 Android 社区中享有盛誉和值得信赖，但是，这并不意味着代码没有恶意或易受攻击。我们 （PerformanC） 理解他们有理由保持代码闭源，但我们认为恰恰相反。

## 优势
- **永久的自由开源软件（FOSS）**：代码完全开源，开发者可自由使用、修改和分发。

## 依赖项
### 主要工具
| 工具            | 描述                                 |
|-----------------|--------------------------------------|
| `Android NDK`   | 适用于Android 原生开发工具包           |

### C++ 依赖项
| 依赖项         | 描述                             |
|----------------|----------------------------------|
| `lsplt`        | 适用于 Android 的简单 PLT Hook    |

## 安装说明
### 1. 选择正确的 ZIP 文件
选择合适的构建/zip 文件至关重要，因为这将决定 ReZygisk 的隐蔽性和稳定性。不过，这并非难事：

- **`release`**：推荐用于正常使用，应该是大多数情况下选择的二进制文件，它会删除应用程序级日志记录并提供更优化的二进制文件。
- **`debug`**：用于调试目的。但是，它提供相反的情况，具有大量日志记录且没有优化，因此，您应该仅将其用于调试目的以及获取用于创建 Issue 的日志。

对于分支，除非开发者另有指示，或者你想测试即将推出的功能并了解其中的风险，否则应始终使用 `main` 分支。

## 2. 刷写 zip

选择正确的版本后，您应该使用当前的根管理器（如 Magisk 或 KernelSU）刷写它。您可以通过转到根管理器的部分并选择您下载的 zip 来执行此作。**`Modules`**

刷写后，检查安装日志以确保没有错误，如果一切正常，您可以重新启动设备。

> [!WARNING]
> Magisk 用户应禁用内置的 Zygisk，因为它会与 ReZygisk 冲突。这可以通过转到 Magisk 部分并禁用该选项来完成。`Settings` `zygisk`

### 3. 验证安装
重启后，你可以通过检查根管理器中模块部分的模块描述来验证 ReZygisk 是否正常工作。描述应显示所需的守护进程正在运行。例如，如果你的环境支持 64 位和 32 位，它将如下所示：
`[monitor: 😋 tracing, zygote64: 😋 injected, daemon64: 😋 running (...) zygote32: 😋 injected, daemon32: 😋 running (...)] Standalone implementation of Zygisk.`

## 翻译贡献
目前有两种不同的方式为 ReZygisk 贡献翻译：

- **README 翻译**：你可以在 `READMEs` 文件夹中创建一个新文件，遵循 `README_<language>.md` 的命名约定，其中 `<language>` 是语言代码（例如，`README_zh-CN.md` 代表中文（中国大陆）），然后向 `main` 分支提交拉取请求。
- **ReZygisk WebUI 翻译**：你首先需要在 [Crowdin](https://crowdin.com/project/rezygisk) 上进行贡献。获得批准后，从那里获取 `.json` 文件，并提交拉取请求，将 `.json` 文件添加到 `webroot/lang` 文件夹，并按字母顺序将你的贡献信息添加到 {insert\_element\_5\_YFRSQU5TTEFUT1IubWRg} 文件中。

## 贡献项目
若要为 ReZygisk 项目做出贡献，你必须遵循 [PerformanC 的贡献指南](https://github.com/PerformanC/contributing)，同时遵守其安全政策、行为准则和语法标准。

## 支持渠道
如果你有任何关于 ReZygisk 或其他 PerformanC 项目的问题，请随时加入以下任何一个渠道：

- **Discord 频道**：[PerformanC](https://discord.gg/uPveNfTuCJ)
- **ReZygisk Telegram 频道**：[@rezygisk](https://t.me/rezygisk)
- **PerformanC Telegram 频道**：[@performancorg](https://t.me/performancorg)
- **PerformanC Signal 群组**：[@performanc](https://signal.group/#CjQKID3SS8N5y4lXj3VjjGxVJnzNsTIuaYZjj3i8UhipAS0gEhAedxPjT5WjbOs6FUuXptcT)

## 许可证
ReZygisk 主要遵循由 Dr - TSNG 发布的 GPL 许可协议，但对于重写的代码，则遵循 The PerformanC Organization 发布的 AGPL 3.0 许可协议。你可以在 [开源倡议组织](https://opensource.org/licenses/AGPL-3.0) 上了解更多相关信息。
