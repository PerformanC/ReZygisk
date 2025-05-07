# ReZygisk

[English](../README.md)

ReZygisk, Zygisk Next'in bir forkudur ve Zygisk'in bağımsız bir uygulamasıdır. KernelSU, Magisk (yerleşik olanın dışında) ve APatch (Çalışmalar Devam Ediyor) için Zygisk API desteği sağlar.

Amacı, C++ ve Rust'tan C diline geçerek kod tabanını modernize etmek ve yeniden yazmaktır. Bu, daha verimli ve hızlı bir Zygisk API uygulaması sağlarken daha esnek bir lisans sunar.

> [!NOT]
> Bu modül/fork şu anda geliştirme aşamasındadır (WIP - Work In Progress); yalnızca Release'deki .zip dosyasını kullanın.
>
> [Actions](https://github.com/PerformanC/ReZygisk/actions) sayfasındaki .zip dosyasını yüklemek tamamen sizin sorumluluğunuzdadır; cihazınız bootloop'a girebilir.

## Neden?

Zygisk Next'in son sürümleri açık kaynaklı değildir ve tamamen geliştiricilerine ayrılmıştır. Bu durum, projeye katkıda bulunma yeteneğimizi sınırlamakla kalmaz, aynı zamanda kodun denetlenmesini imkansız hale getirir. Bu, Zygisk Next'in süper kullanıcı (root) ayrıcalıklarıyla çalışması ve tüm sisteme erişimi olması nedeniyle büyük bir güvenlik sorunudur.

Zygisk Next geliştiricileri, Android topluluğunda tanınmış ve güvenilir kişilerdir, ancak bu, kodun kötü niyetli veya hassas olmadığını garanti etmez. PerformanC olarak, kodu kapalı kaynaklı tutma nedenlerini anlasak da, bunun tersini savunuyoruz.

## Avantajlar

- Sonsuza kadar açık kaynak (FOSS)

## Bağımlılıklar

| Araç             | Açıklama                             |
|------------------|--------------------------------------|
| `Android NDK`    | Android için Yerel Geliştirme Kiti   |

### C++ Bağımlılıkları

| Bağımlılık | Açıklama                        |
|------------|---------------------------------|
| `lsplt`    | Android için Basit PLT Hook     |

## Kullanım

Şu anda geliştirme aşamasındayız. (Yakında)

## Kurulum

Şu anda mevcut kararlı sürüm yoktur. (Yakında)

## Çeviri

Şu anda başka bir platformla entegre bir çeviri sistemimiz bulunmuyor, ancak [add/new-webui](https://github.com/PerformanC/ReZygisk/tree/add/new-webui) branch'ine katkıda bulunabilirsiniz. Lütfen GitHub profilinizi [TRANSLATOR.md](https://github.com/PerformanC/ReZygisk/blob/add/new-webui/TRANSLATOR.md) dosyasına eklemeyi unutmayın, böylece katkılarınız görülebilir.

## Destek
ReZygisk veya diğer PerformanC projeleriyle ilgili herhangi bir soru için aşağıdaki kanallardan herhangi birine katılabilirsiniz:

- Discord Kanalı: [PerformanC](https://discord.gg/uPveNfTuCJ)
- ReZygisk Telegram Kanalı: [@rezygisk](https://t.me/rezygisk)
- PerformanC Telegram Kanalı: [@performancorg](https://t.me/performancorg)

## Katkıda Bulunma

ReZygisk'e katkıda bulunmak için PerformanC'nin [Katkı Yönergeleri](https://github.com/PerformanC/contributing)'ni takip etmek zorunludur. Güvenlik Politikası, Davranış Kuralları ve sözdizimi standartlarına uyulmalıdır.

## Lisans

ReZygisk, büyük ölçüde Dr-TSNG tarafından GPL lisansı altında, ancak yeniden yazılmış kodlar için The PerformanC Organization tarafından AGPL 3.0 lisansı altında lisanslanmıştır. Daha fazla bilgi için [Open Source Initiative](https://opensource.org/licenses/AGPL-3.0)'e göz atabilirsiniz.
