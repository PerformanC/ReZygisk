# ReZygisk

[English](../README.md)

ReZygisk é uma fork do Zygisk Next, uma implementação do Zygisk independente, fornecendo a API do Zygisk para o KernelSU, APatch e Magisk (além do embutido).

Ele foca em modernizar e re-escrever todo o código fonte para C, permitindo uma implementação da API do Zygisk com uma licença mais permissiva e amigável a FOSS.

## Por quê?

Os últimos lançamentos do Zygisk Next não possuem código aberto, reservando-o para os seus desenvolvedores. Isso não só limita nossa capacidade de contribuir com o projeto, mas também impossibilita a auditoria do código, uma preocupação grave de segurança, já que o Zygisk Next é um módulo que roda como superuser (super usuário/root), tendo acesso a todo o sistema.

Os desenvolvedores do Zygisk Next são famosos e confiados pela comunidade Android, mas isso não significa que o código não seja nem malicioso nem vulnerável. Nós (PerformanC) reconhecemos seus motivos de manterem o código recluso a eles, mas a gente acredita no contrário.

## Vantagens

- FOSS (Pra sempre)

## Dependências

| Ferramenta      | Descrição                                    |
|-----------------|----------------------------------------------|
| `Android NDK`   | Kit de Desenvolvimento Nativo para o Android |

### Dependências C++

| Dependência | Descrição                        |
|-------------|----------------------------------|
| `lsplt`     | PLT Hook simples para o Android  |

## Instalação

### 1. Selecionando o zip apropriado

A seleção da build/zip é importate, já que vai determinar o quão escondido e estável o ReZygisk vai ser. Isso, no entanto, não é uma tarefa difícil:

- `release` deve ser a escolha para a maioria dos casos, ele remove o log de nível de app e oferece binários mais otimizados.
- `debug`, no entanto, oferece o oposto, com logs extensos, e sem otimizações. Por isso, **você deve usar apenas para fins de depuração** e **ao obter logs para criar um Issue**.

### 2. "Flash"ando o zip

Depois de escolher a build apropriada, você deve "flashar" ela usando seu gerenciador de root atual, como o Magisk ou o KernelSU. Você pode fazer isso indo na seção `Módulos` do seu gerenciador de root e selecionando o zip que você fez download.

Depois de "flashar", confira os logs de instalação para garantir que não houve erros, e se tudo estiver certo, você pode reiniciar seu dispositivo.

> [!WARNING]
> Usuários do Magisk devem desabilitar o Zygisk embutido, já que ele vai conflitar com o ReZygisk. Isso pode ser feito indo na seção `Configurações` do Magisk e desabilitando a opção `Zygisk`.

### 3. Verificando a instalação

Depois de reiniciar, você pode verificar se o ReZygisk está funcionando corretamente indo na seção `Módulos` do seu gerenciador de root. A descrição deve indicar que os daemons necessários estão rodando. Por exemplo, se seu ambiente suporta tanto 64-bit quanto 32-bit, deve estar parecido com isso: `[monitor: 😋 tracing, zygote64: 😋 injected, daemon64: 😋 running (...) zygote32: 😋 injected, daemon32: 😋 running (...)] Standalone implementation of Zygisk.`

## Tradução

Tem duas formas diferentes de contribuir com traduções para o ReZygisk:

- Para traduções do README, você pode criar um novo arquivo na pasta `READMEs`, seguindo a padronização de nome de `README_<idioma>.md`, onde `<idioma>` é o código do idioma (ex: `README_pt-BR.md` para português brasileiro), e abrir um pull request para o branch `main` com suas mudanças.
- Para traduções da WebUI do ReZygisk, você deve primeiro contribuir no nosso [Crowdin](https://crowdin.com/project/rezygisk). Depois de aprovado, pegue o arquivo `.json` de lá e abra um pull request com suas mudanças -- adicionando o arquivo `.json` na pasta `webroot/lang` e seus créditos no arquivo `TRANSLATOR.md`, em ordem alfabética.

## Suporte

Para quaisquer problemas no ReZygisk ou qualquer projeto da PerformanC, sinta-se livre para entrar em qualquer canal abaixo:

- Server do Discord: [PerformanC](https://discord.gg/uPveNfTuCJ)
- Canal do Telegram do ReZygisk: [@rezygisk](https://t.me/rezygisk)
- Canal do Telegram da PerformanC: [@performancorg](https://t.me/performancorg)
- Grupo do Signal da PerformanC: [@performanc](https://signal.group/#CjQKID3SS8N5y4lXj3VjjGxVJnzNsTIuaYZjj3i8UhipAS0gEhAedxPjT5WjbOs6FUuXptcT)

## Contribuição

É obrigatório seguir as [Regras de Contribuição](https://github.com/PerformanC/contributing) da PerformanC para contribuir ao ReZygisk, seguindo sua Política de Segurança, Código de Conduta, e padronização de sintaxe.

## Licença

ReZygisk é majoritamente licenciado em GPL, por Dr-TSNG, mas também em AGPL 3.0, pela A Organização PerformanC (The PerformanC Organization) para código re-escrito. Você pode ler mais em [Open Source Initiative](https://opensource.org/licenses/AGPL-3.0).
