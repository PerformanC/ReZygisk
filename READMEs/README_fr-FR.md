# ReZygisk

[English](../README.md)

ReZygisk est un fork de Zygisk Next, une implémentation autonome de Zygisk. Il vise à fournir un support de l'API Zygisk pour KernelSU, APatch et Magisk (Officiel et Kitsune).

L'objectif est de moderniser et de réécrire la base du code initégralement en C. Cela permettra une meilleure efficacité et une implémentation plus rapide de l'API Zygisk, le tout sous une licence plus permissive et en faveur des logiciels libres (FOSS).

## Pourquoi ?

La dernière release de Zygisk Next n'est pas open source, le code est donc accessible uniquement à ses développeurs. Non seulement cela limite notre capacité à contribuer au projet, mais cela rend également impossible la vérification du code, ce qui constitue une préoccupation majeure en matière de sécurité. Zygisk Next est un module fonctionnant avec les permissions administrateur (root) et a donc accès à l'entièreté du système.

Les développeurs de Zygisk Next sont connus et reconnus dans la communauté Android. Toutefois, cela ne signifie pas que du code malveillant ou des vulnérabilités ne se cachent pas dans le code. Nous (PerfomanC) comprenons qu'ils aient des raisons de garder leur code en source fermée, mais nous pensons qu'avoir un code open source est mieux.

## Avantages

- FOSS (Pour toujours !)

## Dépendances

| Outil         | Description                          |
|---------------|--------------------------------------|
| `Android NDK` | Kit de développement natif d'Android |

### Dépendances C++

| Dépendance | Description                  |
|------------|------------------------------|
| `PLTI`     | Simple PLT Hook pour Android |

## Installation

### 1. Choisi la bonne archive ZIP

La sélection du build/archive ZIP est important, car cela déterminera à quel point ReZygisk sera caché et stable. Toutefois, ce n'est pas compliqué :

- `release` doit être choisie dans la majorité des cas, car elle supprime les journaux au niveau application et offre des binaires plus optimisés.
- `debug`, en revanche, cette version offre l'inverse avec des journaux détaillés et aucune optimisation. C'est pour cela que **vous ne devriez n'utiliser cette version uniquement pour le débogage** et **l'obtention de journaux pour ouvrir un rapport d'incident (issue Github)**.

En ce qui concerne les branches, vous devriez toujours utiliser la branche `main`, sauf si les développeurs vous indiquent le contraire ou si vous souhaitez tester les fonctionnalités à venir et êtes conscient des risques encourus.

### 2. Flashez l'archive zip

Après avoir choisi le bon build, vous devez le flasher à l'aide de votre gestionnaire root, comme Magisk ou KernelSU. Vous pouvez le faire en allant dans la section `Modules` de votre gestionnaire root et en y sélectionnant l'archive zip que vous venez de télécharger.

Après le flash, vérifiez les journaux d'installation pour vous assurer qu'il n'y ait pas d'erreurs, et si tout va bien, vous pouvez redémarrer votre appareil.

> [!WARNING]
> Les utilisateurs de Magisk doivent désactiver Zygisk pré intégré, car sinon il entrera en conflit avec ReZygisk. Cela peut être fait en vous rendant dans la section `Paramètres` de Magisk et en désactivant l'option `Zygisk`

### 3. Vérifiez l'installation

Après le redémarrage, vous ne pouvez pas vérifier si ReZygisk fonctionne normalement en vérifiant la description du moudles dans la section `Modules` de votre gestionnaire root. La description doit indiquer que les processus en arrière plan nécessaire sont en cours d'exécution. Par exemple, si votre environnement prend en charge à la fois le 64 bits et le 32 bits, cela devrait ressembler à ceci :`[Monitor: ✅, ReZygisk 64-bit: ✅, ReZygisk 32-bit: ✅] Standalone implementation of Zygisk.`

## Traduction

Il existe actuellement deux façons différentes de contribuer aux traductions pour ReZygisk:

- Pour les traductions du README, vous pouvez créer un nouveau fichier dans le dossier READMEs, en suivant la convention de dénomination des fichiers `README_<langue>.md`, où `<langue>` est le code de la langue (par exemple, `README_fr-FR.md` pour le franco français), puis ouvrir un pull request vers la branche `main` avec vos modifications.
- Pour les traductions de l'interface WebUI de ReZygisk, vous devez passer par le projet [Crowdin](https://crowdin.com/project/rezygisk). Une fois approuvé, récupérez le fichier `.json` et ouvrez un pull request avec vos modifications -- en ajoutant le fichier `.json` au dossier `webroot/lang` et vos crédits au fichier `TRANSLATOR.md`, par ordre alphabétique.

## Support

Pour toutes questions relatives a ReZygisk ou d'autres projets de PerformanC, n'hésitez pas à nous rejoindre via les différents moyens disponibles:

- Notre Discord: [PerformanC](https://discord.gg/uPveNfTuCJ)
- Le Telegram relatif a ReZygisk: [@rezygisk](https://t.me/rezygisk)
- Notre Telegram: [@performancorg](https://t.me/performancorg)
- Notre Signal: [@performanc](https://signal.group/#CjQKID3SS8N5y4lXj3VjjGxVJnzNsTIuaYZjj3i8UhipAS0gEhAedxPjT5WjbOs6FUuXptcT)

## Contribution

Il est obligatoire de lire les instructions de PerformanC dans les [Contribution Guidelines](https://github.com/PerformanC/contributing) afin de contribuer au projet ReZygisk. Suivez la politique de sécurité, le code de conduite et les standards relatif à la syntaxe.

## License

ReZygisk est majoritairement sous la licence GPL pour la partie de Dr-TSNG, mais sous licence AGPL 3.0 pour la partie réécrite du code par PerformanC. Vous pouvez trouver plus d'information sur le lien suivant : [Open Source Initiative](https://opensource.org/licenses/AGPL-3.0).
