# ReZygisk

[English](../README.md)

ReZygisk는 KernelSU, APatch 그리고 Magisk(공식 및 Kitsune)에 대한 Zygisk API 지원을 제공하는 독립형 Zygisk 구현체인 Zygisk Next의 포크 프로젝트입니다.

이 프로젝트는 코드베이스를 완전히 C언어로 재작성하고 현대화하여, 더 허용적이고 FOSS(자유-오픈 소스 소프트웨어) 친화적인 라이선스 내에서 Zygisk API를 더 효율적이고 빠르게 구현하는 것을 목표로 합니다.

## 왜 만들었나요?

Zygisk Next의 최신 릴리스는 오픈 소스가 아니며, 개발자가 코드를 전적으로 독점하고 있습니다. 이는 프로젝트에 대한 기여를 제한할 뿐만 아니라, 코드를 감사하는 것을 불가능하게 만듭니다. Zygisk Next는 전체 시스템에 접근할 수 있는 슈퍼유저(ROOT) 권한으로 실행되는 모듈이기 때문에, 이는 주요 보안 우려 사항입니다.

Zygisk Next 개발자들은 Android 커뮤니티에서 유명하고 신뢰받고 있지만, 그렇다고 해서 해당 코드가 악의적이거나 취약점이 없다는 의미는 아닙니다. 저희(PerformanC)는 그들이 코드를 비공개로 유지하는 이유가 있음을 이해하지만, 그와 반대되는 신념을 갖고 있습니다.

## 장점

- FOSS (평생)

## 의존성

| 도구            | 설명                                   |
|-----------------|----------------------------------------|
| `Android NDK`   | Android용 네이티브 개발 키트           |

### C언어 의존성

| 의존성      | 설명                          |
|-------------|-------------------------------|
| `LSPLt`     | Android용 간단 PLT Hook       |
| `CSOLoader` | 최첨단 Linux 커스텀 Linker    |

## 설치

### 1. 올바른 zip 파일 선택

빌드/zip 파일 선택은 ReZygisk가 얼마나 은밀하고 안정적일지를 결정하므로 중요합니다. 하지만 어렵지는 않습니다.

- `release`: 대부분의 경우에 선택해야 하며, 앱 수준의 로깅을 제거하고 더 최적화된 바이너리를 제공합니다.
- `debug`: 반대로 로깅이 많고 최적화가 되어 있지 않습니다. 따라서 **디버깅 목적**이나 **이슈 생성을 위해 로그를 수집할 때만** 사용해야 합니다.

브랜치의 경우, 개발자가 별도로 안내하지 않는 한, 또는 곧 출시될 기능을 테스트하고 관련 위험을 감수하려는 경우가 아니라면 항상 `main` 브랜치를 사용해야 합니다.

### 2. zip 파일 플래싱

올바른 빌드를 선택했다면, Magisk나 KernelSU와 같은 현재 사용 중인 ROOT 관리자를 사용하여 플래싱해야 합니다. ROOT 관리자의 `모듈` 섹션으로 이동하여 다운로드한 zip 파일을 선택하면 됩니다.

플래싱 후 설치 로그에 오류가 없는지 확인하고, 문제가 없다면 기기를 재부팅하세요.

> [!WARNING]
> Magisk 사용자는 내장된 Zygisk와 충돌할 수 있으므로 이를 비활성화해야 합니다. Magisk의 `설정` 섹션으로 이동하여 `Zygisk` 옵션을 비활성화하면 됩니다.

### 3. 설치 확인

재부팅 후, ROOT 관리자의 `모듈` 섹션에서 모듈 설명을 확인하여 ReZygisk가 올바르게 작동하는지 확인할 수 있습니다. 설명에는 필요한 데몬이 실행 중임이 표시되어야 합니다. 예를 들어, 64비트와 32비트를 모두 지원하는 환경이라면 다음과 같이 보일 것입니다: `[Monitor: ✅, ReZygisk 64-bit: ✅, ReZygisk 32-bit: ✅] Standalone implementation of Zygisk.`

## 번역

현재 ReZygisk 번역에 기여하는 방법은 두 가지가 있습니다.

- README 번역: `READMEs` 폴더에 `README_<언어코드>.md` 형식(예: 브라질 포르투갈어의 경우 `README_pt-BR.md`)으로 새 파일을 만들고, 변경 사항을 `main` 브랜치에 Pull Request로 보냅니다.
- ReZygisk WebUI 번역: 먼저 [Crowdin](https://crowdin.com/project/rezygisk)에 기여해 주세요. 승인되면 그곳에서 `.json` 파일을 가져와 변경 사항과 함께 풀 리퀘스트를 엽니다. 이때 `.json` 파일은 `webroot/lang` 폴더에 추가하고, `TRANSLATOR.md` 파일에 본인의 크레딧을 알파벳 순서로 추가해야 합니다.

## 지원

ReZygisk나 다른 PerformanC 프로젝트와 관련된 질문이 있다면, 아래 채널 중 하나에 자유롭게 참여해 주세요.

- Discord 채널: [PerformanC](https://discord.gg/uPveNfTuCJ)
- ReZygisk 텔레그램 채널: [@rezygisk](https://t.me/rezygisk)
- PerformanC 텔레그램 채널: [@performancorg](https://t.me/performancorg)
- PerformanC Signal 그룹: [@performanc](https://signal.group/#CjQKID3SS8N5y4lXj3VjjGxVJnzNsTIuaYZjj3i8UhipAS0gEhAedxPjT5WjbOs6FUuXptcT)

## 기여

ReZygisk에 기여하려면 PerformanC의 [기여 가이드라인](https://github.com/PerformanC/contributing)을 반드시 준수해야 합니다. 여기에는 보안 정책, 행동 강령 및 구문 표준을 따르는 것이 포함됩니다.

## 라이선스

ReZygisk는 주로 Dr-TSNG에 의한 GPL 라이선스를 따르지만, 재작성된 코드에 대해서는 The PerformanC Organization에 의한 AGPL 3.0 라이선스도 적용됩니다. 자세한 내용은 [Open Source Initiative](https://opensource.org/licenses/AGPL-3.0)에서 확인할 수 있습니다.
