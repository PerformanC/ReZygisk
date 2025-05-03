# ReZygisk

[简体中文](/READMEs/README_zh-CN.md)|[繁體中文](/READMEs/README_zh-TW.md)|[Bahasa Indonesia](/READMEs/README_id-ID.md)|[Tiếng Việt](/READMEs/README_vi-VN.md)|[Português Brasileiro](/READMEs/README_pt-BR.md)|[Türkçe](/READMEs/README_tr-TR.md)|[French](/READMEs/README_fr-FR.md)

ReZygisk è un fork di Zygisk Next, una implementazione di Zygisk, che da supporto della Zygisk API per KernelSU, Magisk (tranne quello già presente), e APatch (In via di sviluppo).

Il suo obbiettivo è modernizzare e riscrivere il codice in C (da C++ e Rust), promettendo un'implementazione più efficente e veloce dell'API Zygisk con una licenza più permissiva.

> [!NOTA]
> Questo modulo/fork è in via di sviluppo; usa solo i .zip dalle Releases.
>
> Anche se potresti installare il .zip da [Actions](https://github.com/PerformanC/ReZygisk/actions), è a tua discrezione installarlo siccome potrebbe causare bootloop.

## Perché?

Le ultime versioni di Zygisk Next non sono open-source, tenendo il codice solo per i developer. Non solo limita la nostra abilità a contribuire al progetto, ma anche impossibilità di vedere il codice, che è un rischio alla sicurezza non indifferente, visto che Zygisk Next ha privilegi superuser (root), avendo accesso all'intero sistema.

I developer di Zygisk Next sono famosi e fidati nella community di Android, però, questo non vuol dire che il codice non sia maligno o vulnerabile. Noi (PerformanC) capiamo che abbiano le loro ragioni per tenere il codicee closed-source, ma pensiamo il contrario.

## Vantaggi

- FOSS (Per sempre)

## Dipendenze

| Tool            | Descrizione                            |
|-----------------|----------------------------------------|
| `Android NDK`   | Kit di sviluppo nativo di Android      |

### Dipendenze di C++

| Dipendenze | Descrizione                      |
|------------|----------------------------------|
| `lsplt`    | Hook di PLT semplice per Android |

## Utilizzo

In questo momento stiamo cucinando (Presto).

## Installazione

Per ora non ci sono release stabili. (Presto)

## Translation

Per ora, non abbiamo integrazione con una piattaforma per le traduzioni, ma puoi contribuire al branch [add/new-webui](https://github.com/PerformanC/ReZygisk/tree/add/new-webui). Perfavore non scordarti di includere il tuo profilo GitHub in [TRANSLATOR.md](https://github.com/PerformanC/ReZygisk/blob/add/new-webui/TRANSLATOR.md) così che si possa vedere il tuo contributo.

## Supporto
Per ogni domanda relativa a ReZygisk o a progetti di PerformanC, entra in uno di questi canali:

- Discord: [PerformanC](https://discord.gg/uPveNfTuCJ)
- Telegram di ReZygisk: [@rezygiskchat](https://t.me/rezygiskchat)
- Telegram di PerformanC: [@performancorg](https://t.me/performancorg)

## Contributo

È obbligatorio seguire le [Linee guida di contributo](https://github.com/PerformanC/contributing) per contribuire in ReZygisk. Seguendo le Polizze di Sicurezza, Codice di Condotta, e normale sintassi.

## Licenza

ReZygisk è sotto licenza GPL, di Dr-TSNG, ma anche AGPL 3.0, da The PerformanC Organization, per il codice riscritto. Puoi leggere di più su [Open Source Initiative](https://opensource.org/licenses/AGPL-3.0).
