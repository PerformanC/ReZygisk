# ReZygisk

[![Latest Releases](https://img.shields.io/github/v/release/PerformanC/ReZygisk?label=Releases&logo=github)](https://github.com/PerformanC/ReZygisk/releases/latest)
[![Repo_License](https://img.shields.io/badge/License-GPL_3.0-orange?logo=gnu)](LICENSE)
[![Localization](https://img.shields.io/badge/Localization-Coming%20Soon-purple)](https://github.com/PerformanC/ReZygisk/blob/02ffcea7d87b98f231ea86979c5ccebfe8502c90/TRANSLATOR.md)

[![GitHub_Build_Status](https://img.shields.io/github/actions/workflow/status/PerformanC/ReZygisk/ci.yml?logo=github&label=Night%20Release)](https://github.com/PerformanC/ReZygisk/actions)
[![GitHub_Issues](https://img.shields.io/github/issues/PerformanC/ReZygisk?logo=github&label=Issues
)](https://github.com/PerformanC/ReZygisk/issues)
[![GitHub_Pull_Requests](https://img.shields.io/github/issues-pr/PerformanC/ReZygisk?logo=github&label=Pull%20Requests&color=pink)](https://github.com/PerformanC/ReZygisk/issues)

[![PerformanC_Discord](https://img.shields.io/discord/1036045973039890522?logo=discord&logoColor=%23FFFFFF&label=Discord&color=%235865F2)](https://discord.gg/uPveNfTuCJ)
[![PerformanC_Telegram](https://img.shields.io/badge/Telegram-PerformanC-%2326A5E4?logo=telegram)](https://t.me/performancorg)
[![ReZygisk_Telegram](https://img.shields.io/badge/Telegram-ReZygisk-%2326A5E4?logo=telegram)](https://t.me/rezygiskchat)

ReZygisk is a fork of Zygisk Next, a standalone implementation of Zygisk, providing Zygisk API support for KernelSU, Magisk (besides built-in), and APatch (Work In Progress).

It aims to modernize and re-write the codebase to C (from C++ and Rust), allowing a more efficient and faster implementation of the Zygisk API with a more permissive license.

> [!NOTE]
> This module/fork is WIP (Work In Progress); Please wait for the stable version to be released on [Releases](https://github.com/PerformanC/ReZygisk/releases/latest) page.
>
> Although you may install the .zip from the [Actions](https://github.com/PerformanC/ReZygisk/actions) page, it is only at your discretion to install it since your device might enter bootloop.

## Why?

The latest releases of Zygisk Next are not open-source, reserving entirely the code for its developers. Not only does that limit our ability to contribute to the project, but it also makes it impossible to audit the code, which is a major security concern, as Zygisk Next is a module that runs with superuser (root) privileges, having access to the entire system.

The Zygisk Next developers are famous and trusted in the Android community, however this doesn't mean that the code is not malicious or vulnerable. We (PerformanC) understand they have their reasons for keeping the code closed-source, but we believe the contrary.

## Advantages

- FOSS (Forever)
- Better Standalone Hiding

## Dependencies

| Tool            | Description                            |
|-----------------|----------------------------------------|
| `rustc`         | The Rust compiler                      |
| `Android NDK`   | Native Development Kit for Android     |

### Rust Dependencies

| Dependency       | Description                                                   |
|------------------|---------------------------------------------------------------|
| `android_logger` | Logger for Android platform                                   |
| `anyhow`         | Error handling library                                        |
| `bitflags`       | Macro to generate structures for bitwise operations           |
| `const_format`   | Compile-time string formatting                                |
| `futures`        | Abstractions for asynchronous programming                     |
| `konst`          | Compile-time constant evaluation                              |
| `lazy_static`    | Macro for declaring lazily evaluated static variables         |
| `libc`           | FFI bindings to native C libraries                            |
| `log`            | Logging facade                                                |
| `memfd`          | Memory file descriptor utilities                              |
| `num_enum`       | Derive macros to convert between numbers and enums            |
| `passfd`         | Passing file descriptors over Unix sockets                    |
| `proc-maps`      | Parse and analyze process memory maps                         |

### C++ Dependencies

| Dependency | Description                   |
|------------|-------------------------------|
| `lsplt`    | Simple PLT Hook for Android   |

## Usage

We're currently in the process of cooking. (Coming Soon)

## Installation

There are currently no available stable releases. (Coming Soon)

## Support
For any question related to ReZygisk or other PerformanC projects, feel free to join any of the following channels below:

- Discord Channel: [PerformanC](https://discord.gg/uPveNfTuCJ)
- ReZygisk Telegram Channel: [@rezygiskchat](https://t.me/rezygiskchat)
- PerformanC Telegram Channel: [@performancorg](https://t.me/performancorg)

## Contribution

It is mandatory to follow PerformanC's [Contribution Guidelines](https://github.com/PerformanC/contributing) to contribute to ReZygisk. Following its Security Policy, Code of Conduct, and syntax standard.

## License

ReZygisk is licensed, primarily under GPL by Dr-TSNG, but also by BSD 2-Clause License for re-written code. You can read more about it on [Open Source Initiative](https://opensource.org/licenses/BSD-2-Clause).
