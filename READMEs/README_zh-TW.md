# ReZygisk
> 繁體中文（README_zh-TW.md）是根據[英文版自述檔案（README.md）](https://github.com/PerformanC/ReZygisk/blob/main/README.md)翻譯，僅供參考以便理解英文內容，翻譯可能滯後。

ReZygisk 是 Zygisk Next 的一個分支，這是一個獨立實現的 Zygisk，為 KernelSU、Magisk（除了內建支援外）和 APatch（開發中）提供 Zygisk API 支援。

此專案致力於用 C 語言重寫原有的 C++ 和 Rust 代碼，藉此以更現代且高效的方式實現 Zygisk API，並採用更寬鬆的授權條款。

> [!NOTE]
> 此模組/分支仍在開發中（WIP）；請僅安裝正式版本的壓縮包。
>
> 雖然你可以從 [Actions](https://github.com/PerformanC/ReZygisk/actions) 頁面安裝 .zip 檔，但若因此導致裝置進入開機循環（Bootloop），後果須自行承擔。

## 為什麼選擇ReZygisk？

最新版本的 Zygisk Next 已轉為閉源，只有核心開發者能查閱完整的原始碼。這不僅限制了其他開發者的貢獻，也無法進行代碼審計。由於 Zygisk Next 是一個以超級使用者（root）權限運行的模組，能夠存取整個系統，若閉源將帶來重大的安全風險。

雖然 Zygisk Next 的開發者在 Android 社群中享有盛譽，並且備受信任，但這並不代表他們的專案就完全沒有任何惡意程式碼或漏洞。我們（PerformanC）理解他們因某些原因選擇保持閉源，但我們堅信開源才是更好的選擇。

## 優勢

- 永遠是自由及開放原始碼軟體（FOSS）

## 依賴項

| 工具            | 說明                                   |
|-----------------|---------------------------------------|
| `Android NDK`   | Android 原生開發工具包                  |

### C++ 依賴項

| 依賴     | 說明                                          |
|----------|----------------------------------------------|
| `lsplt`  | Android 的簡單 PLT（程式連結表） 勾取           |

## 用法

我們目前正在開發中。（敬請期待）

## 安裝

目前沒有穩定版本可供下載。（敬請期待）

## 翻譯

目前我們尚未與其他平台整合進行翻譯，但您可以為 [add/new-webui](https://github.com/PerformanC/ReZygisk/tree/add/new-webui)分支做出貢獻。請別忘了在 [TRANSLATOR.md](https://github.com/PerformanC/ReZygisk/blob/add/new-webui/TRANSLATOR.md) 中包含您的 GitHub 個人檔案，讓大家能夠看到您的貢獻。

## 支援
如有關於 ReZygisk 或其他 PerformanC 專案的任何問題，歡迎加入以下任一頻道：

- Discord 頻道: [PerformanC](https://discord.gg/uPveNfTuCJ)
- ReZygisk Telegram 頻道: [@rezygisk](https://t.me/rezygisk)
- PerformanC Telegram 頻道: [@performancorg](https://t.me/performancorg)

## 貢獻

要為 ReZygisk 貢獻，必須遵循 PerformanC 的[貢獻指南](https://github.com/PerformanC/contributing)，並遵守其安全政策、行為準則以及語法標準。

## 授權條款

在 ReZygisk 專案中，舊的 Zygisk Next 部分採用了 GPL 授權，而由 PerformanC 組織重寫的程式碼則採用 AGPL 3.0 授權。

您可以在[開放原始碼倡議（Open Source Initiative）](https://opensource.org/licenses/AGPL-3.0)上閱讀更多相關資訊。
