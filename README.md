# ReZygisk

[Bahasa Indonesia](/READMEs/README_id-ID.md)|[Tiếng Việt](/READMEs/README_vi-VN.md)|[Português Brasileiro](/READMEs/README_pt-BR.md)|[French](/READMEs/README_fr-FR.md)|[日本語](/READMEs/README_ja-JP.md)|[العربية السعودية](/READMEs/README_ar-SA.md)

ReZygisk is a fork of Zygisk Next, a standalone implementation of Zygisk, providing Zygisk API support for KernelSU, APatch and Magisk (Official and Kitsune).

It aims to modernize and re-write the codebase to C entirely, allowing a more efficient and faster implementation of the Zygisk API with a more permissive, and FOSS friendly, license.

## Why?

The latest releases of Zygisk Next are not open-source, reserving entirely the code for its developers. Not only does that limit our ability to contribute to the project, but also impossibilities the audit of the code, which is a major security concern, as Zygisk Next is a module that runs with superuser (root) privileges, having access to the entire system.

The Zygisk Next developers are famous and trusted in the Android community, however, this doesn't mean that the code is not malicious or vulnerable. We (PerformanC) understand they have their reasons to keep the code closed-source, but we believe the contrary.

## Advantages

- FOSS (Forever)

## Dependencies

| Tool            | Description                            |
|-----------------|----------------------------------------|
| `Android NDK`   | Native Development Kit for Android     |

### C Dependencies

| Dependency  | Description                   |
|-------------|-------------------------------|
| `LSPLt`     | Simple PLT Hook for Android   |
| `CSOLoader` | SOTA Linux custom linker      |

## Installation

### 1. Select the right zip

The selection of the build/zip is important, as it will determine how hidden and stable ReZygisk will be. This, however, is not a hard task:

- `release` should be the one chosen for most cases, it removes app-level logging and offers more optimized binaries.
- `debug`, however, offers the opposite, with heavy logging and no optimizations, For this reason, **you should only use it for debugging purposes** and **when obtaining logs for creating an Issue**.

As for branches, you should always use the `main` branch, unless told otherwise by the developers, or if you want to test upcoming features and are aware of the risks involved.

### 2. Flash the zip

After choosing the right build, you should flash it using your current root manager, like Magisk or KernelSU. You can do this by going to the `Modules` section of your root manager and selecting the zip you downloaded.

After flashing, check the installation logs to ensure there are no errors, and if everything is fine, you can reboot your device.

> [!WARNING]
> Magisk users should disable built-in Zygisk, as it will conflict with ReZygisk. This can be done by going to the `Settings` section of Magisk and disabling the `Zygisk` option.

### 3. Verify the installation

After rebooting, you can verify if ReZygisk is working properly by checking the module description in the `Modules` section of your root manager. The description should indicate that the necessary daemons are running. For example, if your environment supports both 64-bit and 32-bit, it should look similar to this: `[Monitor: ✅, ReZygisk 64-bit: ✅, ReZygisk 32-bit: ✅] Standalone implementation of Zygisk.`

## Translation

There are currently two different ways to contribute translations for ReZygisk:

- For translations of the README, you can create a new file in the `READMEs` folder, following the naming convention of `README_<language>.md`, where `<language>` is the language code (e.g., `README_pt-BR.md` for Brazilian Portuguese), and open a pull request to the `main` branch with your changes.
- For translations of the ReZygisk WebUI, you should first contribute to our [Crowdin](https://crowdin.com/project/rezygisk). Once approved retrieve the `.json` file from there and open a pull request with your changes -- adding the `.json` file to the `webroot/lang` folder and your credits to the `TRANSLATOR.md` file, in alphabetic order.

## Support

For any question related to ReZygisk or other PerformanC projects, feel free to join any of the following channels below:

- Discord Channel: [PerformanC](https://discord.gg/uPveNfTuCJ)
- ReZygisk Telegram Channel: [@rezygisk](https://t.me/rezygisk)
- PerformanC Telegram Channel: [@performancorg](https://t.me/performancorg)
- PerformanC Signal Group: [@performanc](https://signal.group/#CjQKID3SS8N5y4lXj3VjjGxVJnzNsTIuaYZjj3i8UhipAS0gEhAedxPjT5WjbOs6FUuXptcT)

## Contribution

It is mandatory to follow PerformanC's [Contribution Guidelines](https://github.com/PerformanC/contributing) to contribute to ReZygisk. Following its Security Policy, Code of Conduct, and syntax standard.

## License

ReZygisk is licensed majoritaly under GPL, by Dr-TSNG, but also AGPL 3.0, by The PerformanC Organization, for re-written code. You can read more about it on [Open Source Initiative](https://opensource.org/licenses/AGPL-3.0).
