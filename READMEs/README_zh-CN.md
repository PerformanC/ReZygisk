# ReZygisk

[English](../README.md)

ReZygisk 是 Zygisk Next 的一个分支（fork），是一个独立实现的 Zygisk，提供对 KernelSU、APatch 和 Magisk（官方版与 Kitsune 版）的 Zygisk API 支持。

其目标是将整个代码库完全现代化，并彻底使用 C 语言重写，从而提供一个更高效、更快速、并拥有更宽松、更符合 FOSS 精神的 Zygisk API 实现。

---

## 为什么？

Zygisk Next 的最新版本已不再开源，所有代码完全由其开发者保留。这不仅限制了我们为项目做贡献的能力，也使得代码无法被审计——这是一个非常严重的安全问题，因为 Zygisk Next 是一个以超级用户（root）权限运行的模块，能够访问整个系统。

尽管 Zygisk Next 的开发者在 Android 社区中知名且值得信任，但这并不意味着其闭源代码不存在恶意行为或安全漏洞。我们（PerformanC）理解他们保持闭源的理由，但我们选择了相反的方向。

---

## 优势

**永远 FOSS（开源）**

---

## 依赖项

| 工具            | 描述             |
| ------------- | -------------- |
| `Android NDK` | Android 原生开发套件 |

### C 依赖项

| 依赖          | 描述                      |
| ----------- | ----------------------- |
| `LSPLt`     | 简单的 Android PLT Hook    |
| `CSOLoader` | 最先进（SOTA）的 Linux 自定义链接器 |

---

## 安装

### 1. 选择正确的 zip 文件

选择正确的构建版本非常重要，它将决定 ReZygisk 的隐藏性及稳定性。不过这并不复杂：

* `release`：大多数情况下应选择该版本。

  * 移除应用级日志
  * 更优化的二进制文件

* `debug`：则相反，包含大量日志且没有任何优化。
  **仅用于调试或收集创建 Issue 所需的日志。**

关于分支，你应始终使用 `main` 分支，除非开发者特别说明，或你希望测试即将发布的功能，并清楚其中的风险。

---

### 2. 刷入 zip

选择好构建版本后，你可以通过当前使用的 root 管理器（如 Magisk 或 KernelSU）刷入 zip。

进入 Root 管理器的 `Modules` 页面，选择你下载的 zip 文件进行安装。

刷入完成后请检查安装日志，确认没有错误。如果一切正常即可重启设备。

> [!WARNING]
> **Magisk 用户必须禁用内置 Zygisk**，否则会与 ReZygisk 冲突。
> 路径：`Magisk` → `Settings` → 禁用 `Zygisk` 选项。

---

### 3. 验证安装

重启后，你可以在 Root 管理器的 `Modules` 页面检查 ReZygisk 是否工作正常。

模块描述应显示所需的守护进程已运行。例如，如果你的系统支持 64 位和 32 位，它应类似：

```
[Monitor: ✅, ReZygisk 64-bit: ✅, ReZygisk 32-bit: ✅] Standalone implementation of Zygisk.
```

---

## 翻译贡献

目前有两种方式为 ReZygisk 贡献翻译：

* **README 翻译**
  在 `READMEs` 文件夹创建新文件，命名格式为：

  ```
  README_<language>.md
  ```

  例如：`README_pt-BR.md`（巴西葡语）
  然后向 `main` 分支提交 PR。

* **ReZygisk WebUI 翻译**
  首先在 Crowdin 贡献翻译：
  👉 [https://crowdin.com/project/rezygisk](https://crowdin.com/project/rezygisk)
  翻译被批准后，从 Crowdin 下载 `.json` 文件，提交 PR：

  * 添加到 `webroot/lang` 文件夹
  * 并按字母顺序将你的名字加入 `TRANSLATOR.md`

---

## 支持

如需咨询 ReZygisk 或其他 PerformanC 项目，可以加入以下频道：

* Discord 频道：
  [PerformanC](https://discord.gg/uPveNfTuCJ)

* ReZygisk Telegram 频道：
  [@rezygisk](https://t.me/rezygisk)

* PerformanC Telegram 频道：
  [@performancorg](https://t.me/performancorg)

* PerformanC Signal 群组：
  [@performanc](https://signal.group/#CjQKID3SS8N5y4lXj3VjjGxVJnzNsTIuaYZjj3i8UhipAS0gEhAedxPjT5WjbOs6FUuXptcT)

---

## 贡献

为 ReZygisk 做贡献前，必须遵守 PerformanC 的
👉 [贡献指南](https://github.com/PerformanC/contributing)

包括其安全政策、行为准则和语法标准。

---

## 许可证

ReZygisk 的主要代码使用 **GPL 许可证**（由 Dr-TSNG 提供），
而重写部分使用 **AGPL 3.0**（由 The PerformanC Organization 提供）。
详情可查看：
👉 [Open Source Initiative](https://opensource.org/licenses/AGPL-3.0)
