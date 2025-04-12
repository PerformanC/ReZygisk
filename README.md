# ReZygisk

[简体中文](/READMEs/README_zh-CN.md)|[繁體中文](/READMEs/README_zh-TW.md)|[Bahasa Indonesia](/READMEs/README_id-ID.md)|[Tiếng Việt](/READMEs/README_vi-VN.md)|[Português Brasileiro](/READMEs/README_pt-BR.md)|[Türkçe](/READMEs/README_tr-TR.md)|[French](/READMEs/README_fr-FR.md)

ReZygisk is a fork of Zygisk Next, a standalone implementation of Zygisk, providing Zygisk API support for KernelSU, Magisk (besides built-in), and APatch (Work In Progress).

It aims to modernize and re-write the codebase to C (from C++ and Rust), allowing a more efficient and faster implementation of the Zygisk API with a more permissive license.

> [!NOTE]
> This module/fork is WIP (Work In Progress); only use .zip from the Releases.
>
> Although you may install the .zip from the [Actions](https://github.com/PerformanC/ReZygisk/actions) page, it is only at your discretion to install it since your device might enter bootloop.

## Why?

The latest releases of Zygisk Next are not open-source, reserving entirely the code for its developers. Not only does that limit our ability to contribute to the project, but also impossibilities the audit of the code, which is a major security concern, as Zygisk Next is a module that runs with superuser (root) privileges, having access to the entire system.

The Zygisk Next developers are famous and trusted in the Android community, however, this doesn't mean that the code is not malicious or vulnerable. We (PerformanC) understand they have their reasons to keep the code closed-source, but we believe the contrary.

## Advantages

- FOSS (Forever)

## Dependencies

| Tool            | Description                            |
|-----------------|----------------------------------------|
| `Android NDK`   | Native Development Kit for Android     |

### C++ Dependencies

| Dependency | Description                   |
|------------|-------------------------------|
| `lsplt`    | Simple PLT Hook for Android   |

## Usage

We're currently in the process of cooking. (Coming Soon)

## Installation

There are currently no available stable releases. (Coming Soon)

## Translation

As of now, we don't have integration with another platform for translations but you may contribute to the [add/new-webui](https://github.com/PerformanC/ReZygisk/tree/add/new-webui) branch. Please don't forget to include your GitHub profile in [TRANSLATOR.md](https://github.com/PerformanC/ReZygisk/blob/add/new-webui/TRANSLATOR.md) so that people can see your contribution.

## Support
For any question related to ReZygisk or other PerformanC projects, feel free to join any of the following channels below:

- Discord Channel: [PerformanC](https://discord.gg/uPveNfTuCJ)
- ReZygisk Telegram Channel: [@rezygiskchat](https://t.me/rezygiskchat)
- PerformanC Telegram Channel: [@performancorg](https://t.me/performancorg)

## Contribution

It is mandatory to follow PerformanC's [Contribution Guidelines](https://github.com/PerformanC/contributing) to contribute to ReZygisk. Following its Security Policy, Code of Conduct, and syntax standard.

## License

ReZygisk is licensed majoritaly under GPL, by Dr-TSNG, but also AGPL 3.0, by The PerformanC Organization, for re-written code. You can read more about it on [Open Source Initiative](https://opensource.org/licenses/AGPL-3.0).
