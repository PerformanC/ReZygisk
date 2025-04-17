# ReZygisk

[English](../README.md)

ReZygisk est un fork de Zygisk Next, une implémentation autonome de Zygisk. Il vise à fournir un support de l'API Zygisk pour KernelSU, Magisk (en plus de l'intégration existante), et pour Apatch (encore en cours de développement).

L'objectif est de moderniser et de réécrire la base du code, initialement en C, vers du C++ et du Rust. Cela permettra une meilleure efficacité et une implémentation plus rapide de l'API Zygisk, le tout sous une licence plus permissive.

> [!NOTE]
> Ce module/fork est en cours de développement ; n'utilisez que les fichiers .zip provenant des 'Releases'.
>
>Bien que vous puissiez installer les fichiers .zip provenant de la page [Actions](https://github.com/PerformanC/ReZygisk/actions), cela vous regarde et peut faire entrer votre téléphone en boucle de redémarrage (bootloop).
## Pourquoi ?

La dernière release de Zygisk Next n'est pas open source, le code est donc accessible uniquement à ses développeurs. Non seulement cela limite notre capacité à contribuer au projet, mais cela rend également impossible la vérification du code, ce qui constitue une préoccupation majeure en matière de sécurité. Zygisk Next est un module fonctionnant avec les permissions administrateur (root) et a donc accès à l'entièreté du système.

Les développeurs de Zygisk Next sont connus et reconnus dans la communauté Android. Toutefois, cela ne signifie pas que du code malveillant ou des vulnérabilités ne se cachent pas dans le code. Nous (PerfomanC) comprenons qu'ils aient des raisons de garder leur code en source fermée, mais nous pensons qu'avoir un code open source est mieux.

## Avantages

- FOSS (Pour toujours !)

## Dépendances

| Outil            | Description                           |
|-----------------|----------------------------------------|
| `Android NDK`   | Kit de développement natif d'Android   |

### Dépendances C++ 

| Dépendance | Description                   |
|------------|-------------------------------|
| `lsplt`    | Simple PLT Hook pour Android  |

## Utilisation

Nous sommes actuellement en train de préparer cela. (Pour bientôt)

## Installation

Il n'y a actuellement pas de version (releases) stable (Pour bientôt)

## Traduction

À ce jour, nous n'avons pas d'intégration avec d'autres plateformes pour traduire, mais vous pouvez contribuer à la branche [add/new-webui](https://github.com/PerformanC/ReZygisk/tree/add/new-webui). Merci de ne pas oublier d'inclure votre profil GitHub dans le fichier [TRANSLATOR.md](https://github.com/PerformanC/ReZygisk/blob/add/new-webui/TRANSLATOR.md) afin que les autres puissent voir votre contribution.

## Support

Pour toutes questions relatives a ReZygisk ou d'autres projets de PerformanC, n'hésitez pas à nous rejoindre via les différents moyens disponibles: 

- Notre Discord: [PerformanC](https://discord.gg/uPveNfTuCJ)
- Le Telegram relatif a ReZygisk: [@rezygiskchat](https://t.me/rezygiskchat)
- Notre Telegram: [@performancorg](https://t.me/performancorg)

## Contribution

Il est obligatoire de lire les instructions de PerformanC dans les [Contribution Guidelines](https://github.com/PerformanC/contributing) afin de contribuer au projet ReZygisk. Suivez la politique de sécurité, le code de conduite et les standards relatif à la syntaxe.

## License

ReZygisk est majoritairement sous la licence GPL pour la partie de Dr-TSNG, mais sous licence AGPL 3.0 pour la partie réécrite du code par PerformanC. Vous pouvez trouver plus d'information sur le lien suivant : [Open Source Initiative](https://opensource.org/licenses/AGPL-3.0).
