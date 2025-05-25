# ReZygisk

[Bahasa Indonesia](/READMEs/README_id-ID.md)|[Tiếng Việt](/READMEs/README_vi-VN.md)|[Português Brasileiro](/READMEs/README_pt-BR.md)|[French](/READMEs/README_fr-FR.md)|[日本語](/READMEs/README_ja-JP.md)|[简体中文](/READMEs/README_zh-CN.md)

ReZygisk 是 Zygisk Next 的一个分支，Zygisk Next 是 Zygisk 的独立实现，它为 KernelSU、APatch 和 Magisk（官方版和 Kitsune 版）提供 Zygisk API 支持。

其目标是将代码库完全重构为 C 语言，以便更高效、快速地实现 Zygisk API，同时采用更宽松且对自由开源软件（FOSS）友好的许可协议。

## 优势
- **永久的自由开源软件（FOSS）**：代码完全开源，开发者可自由使用、修改和分发。

## 依赖项
### 主要工具
| 工具            | 描述                                 |
|-----------------|--------------------------------------|
| `Android NDK`   | Android 原生开发工具包               |

### C++ 依赖项
| 依赖项         | 描述                             |
|----------------|----------------------------------|
| `lsplt`        | 适用于 Android 的简单 PLT 钩子    |

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

## 安装说明
### 1. 选择正确的 ZIP 文件
选择合适的构建/zip 文件至关重要，因为这将决定 ReZygisk 的隐蔽性和稳定性。不过，这并非难事：

- **`release`**：推荐用于正常使用。二进制文件经过优化，日志记录最少。
- **`debug`**：用于调试目的。日志记录完整，无优化处理。

对于分支，除非开发者另有指示，或者你想测试即将推出的功能并了解其中的风险，否则应始终使用 `main` 分支。

### 3. 验证安装
重启后，你可以通过检查根管理器中模块部分的模块描述来验证 ReZygisk 是否正常工作。描述应显示所需的守护进程正在运行。例如，如果你的环境支持 64 位和 32 位，它将如下所示：
`[monitor: 😋 tracing, zygote64: 😋 injected, daemon64: 😋 running (...) zygote32: 😋 injected, daemon32: 😋 running (...)] Standalone implementation of Zygisk.`

## 许可证
ReZygisk 主要遵循由 Dr - TSNG 发布的 GPL 许可协议，但对于重写的代码，则遵循 The PerformanC Organization 发布的 AGPL 3.0 许可协议。你可以在 [开源倡议组织](https://opensource.org/licenses/AGPL-3.0) 上了解更多相关信息。
