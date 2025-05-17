# ReZygisk

[Bahasa Indonesia](/READMEs/README_id-ID.md)|[Tiếng Việt](/READMEs/README_vi-VN.md)|[Português Brasileiro](/READMEs/README_pt-BR.md)|[French](/READMEs/README_fr-FR.md)

ReZygiskはZygiskのスタンドアローン実装であるZygisk Nextのフォークです。ReZygiskは、KernelSU、APatch、Magisk（オフィシャルバージョンとKitsuneバージョン両方）それぞれへのZygisk APIサポートを備えています。

ReZygiskはコードベースをCに移行し、よりモダンなコードで書き換えることを目標にしています。これにより、Zygisk APIのより効率的かつ高速な実装と、FOSSライセンスの両方を備えることができています。

## なぜReZygiskを選ぶべきか

Zygisk Nextの最新リリースはオープンソースではなく、コードをその開発者のみにアクセス可能にしています。これは我々のように一般の開発者の貢献を無下にするだけでなく、Zygisk Nextがroot権限で走るアプリなのにもかかわらずコードにアクセスできないため、セキュリティ上でも深刻な問題が有ります。

Zygisk Nextの開発者達は有名かつコミュニティからも信頼されていますが、これはコードが100%悪意が無いことや脆弱性が無いことを意味しません。我々（PerformanC）は彼らがZygisk Nextをクローズドソースにする理由も理解していますが、我々はその逆を信じます。

## メリット

- FOSS (無制限)

## 依存関係

| ツール           | 説明                                   |
|-----------------|----------------------------------------|
| `Android NDK`   | Native Development Kit for Android     |

### C++ 依存関係

| 依存関係    | 説明                           |
|------------|-------------------------------|
| `lsplt`    | Simple PLT Hook for Android   |

## インストール

### 1. 必要なZipファイルを選択

ReZygiskの安定性や匿名性のためには、ビルドファイル/Zipファイルの選択は**非常に重要**です。しかしながら、これはそこまで難しくもありません。

- `release` バージョンが基本的にはおすすめです。アプリレベルのログが出力されなかったりなど、より効率化されたバイナリが提供されるためです。
- `debug` バージョンはreleaseバージョンの反対です。重いログの出力がなされたり、高速化されていないバイナリが提供されます。このため、このバージョンは**デバッグ用に**、もしくは**Issueを作るためにログを入手する**ときのみに使われるべきです。

ブランチに関しては、基本的に`main`ブランチを選択すべきです。しかしながら、PerformanCの開発者に違うブランチを使うように言われたり、あなたがベータ版のコードを使うことのリスクを理解しかつ実装されたばかりの機能を使いたいのならば違うブランチを選択することも選択肢の一つでしょう。

### 2. Zipファイルをフラッシュ

正しいビルドを選択したあとは、ReZygiskのビルドを現在使用しているルートマネージャー（MagiskやKernelSU等）を使用してフラッシュしてください。これは、マネージャーで`Modules`セクションを開きダウンロードしたビルドファイルを選択することでできます。

フラッシュしたあとは、インストールログを確認して、エラーがないか確かめてください。なんのエラーも起きていなければ、デバイスを再起動してください。

> [!WARNING]
> Magiskを使用しているのならば、ビルトインのZygiskがReZygiskと競合するため無効化してください。Magiskの`設定`セクションを開き、Zygiskオプションを無効化することでできます。
### 3. インストールを確認

再起動後、ルートマネージャーの`Modules`セクションをチェックすることによりReZygiskが正常に動いているかどうか確認できます。
説明欄は、必要なデーモンが動作していることを示しているはずです。例えば、あなたの端末が64bitと32bitの両方をサポートしている場合、右記のように見えるはずです: `[monitor: 😋 tracing, zygote64: 😋 injected, daemon64: 😋 running (...) zygote32: 😋 injected, daemon32: 😋 running (...)] Standalone implementation of Zygisk.`

## 翻訳

There are currently two different ways to contribute translations for ReZygisk:

- READMEの翻訳は、`READMEs`フォルダに`README_<language code>.md`というファイルを作り、そこに翻訳を書き込んでください。その後、プルリクエストを送信してくださいlang` folder and your credits to the `TRANSLATOR.md` file, in alphabetic order.
- ReZygisk WebUIの翻訳のためには、まず[Crowdin](https://crowdin.com/project/rezygisk)で貢献する必要が有ります。一度貢献を許可され、`.json`ファイルを入手したならば、そのファイルを元に新しい言語のファイルを作り、その`.json`ファイルを`webroot/lang`フォルダに入れてください。更に、TRANSLATOR.mdにあなたのクレジットを付与するのも忘れないでください！（なお名前の順番はアルファベット順です）

## サポート

ReZygiskやPerformanCのプロジェクトに関して質問がある場合、以下のいずれかに参加して質問してください。

- Discord チャンネル: [PerformanC](https://discord.gg/uPveNfTuCJ)
- ReZygisk Telegram チャンネル: [@rezygisk](https://t.me/rezygisk)
- PerformanC Telegram チャンネル: [@performancorg](https://t.me/performancorg)
- PerformanC Signal Group: [@performanc](https://signal.group/#CjQKID3SS8N5y4lXj3VjjGxVJnzNsTIuaYZjj3i8UhipAS0gEhAedxPjT5WjbOs6FUuXptcT)

## 貢献

[Contribution Guidelines](https://github.com/PerformanC/contributing)に従ってください。セキュリティポリシー、コードスタイル等、すべて従う必要が有ります。

## ライセンス

ReZygiskはDr-TSNGによるGPLライセンスと、PerformanCが書き直したコードに関してはThe PerformanC OrganizationによるAGPL 3.0ライセンスの元に配布されます。[Open Source Initiative](https://opensource.org/licenses/AGPL-3.0)で、より詳しい情報を得ることができます。
