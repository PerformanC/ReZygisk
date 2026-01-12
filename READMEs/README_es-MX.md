# ReZygisk

[Bahasa Indonesia](/READMEs/README_id-ID.md)|[Tiếng Việt](/READMEs/README_vi-VN.md)|[Português Brasileiro](/READMEs/README_pt-BR.md)|[Français](/READMEs/README_fr-FR.md)|[日本語](/READMEs/README_ja-JP.md)|[العربية السعودية](/READMEs/README_ar-SA.md)|[English](/README.md)

ReZygisk es un fork de Zygisk Next, una implementación independiente de Zygisk que proporciona soporte de API Zygisk para KernelSU, APatch y Magisk (Oficial y Kitsune).

Su objetivo es modernizar y reescribir el código base completamente en C, permitiendo una implementación más eficiente y rápida de la API de Zygisk con una licencia más permisiva y amigable con el FOSS (Software Libre y de Código Abierto).

## ¿Por qué?

Las últimas versiones de Zygisk Next no son de código abierto, reservando el código enteramente para sus desarrolladores. Esto no solo limita nuestra capacidad para contribuir al proyecto, sino que también imposibilita la auditoría del código, lo cual es una gran preocupación de seguridad, ya que Zygisk Next es un módulo que se ejecuta con privilegios de superusuario (root), teniendo acceso a todo el sistema.

Los desarrolladores de Zygisk Next son famosos y de confianza en la comunidad de Android; sin embargo, esto no significa que el código no pueda ser malicioso o vulnerable. Nosotros (PerformanC) entendemos que tienen sus razones para mantener el código cerrado, pero nosotros creemos en lo contrario.

## Ventajas

- FOSS (Para siempre)

## Dependencias

| Herramienta     | Descripción                            |
|-----------------|----------------------------------------|
| `Android NDK`   | Kit de Desarrollo Nativo para Android  |

### Dependencias C

| Dependencia | Descripción                   |
|-------------|-------------------------------|
| `LSPLt`     | Hook PLT simple para Android  |
| `CSOLoader` | Linker personalizado SOTA para Linux |

## Instalación

### 1. Selecciona el zip correcto

La selección de la compilación/zip es importante, ya que determinará qué tan oculto y estable será ReZygisk. Sin embargo, no es una tarea difícil:

- `release`: Debería ser la opción elegida para la mayoría de los casos; elimina el registro (logging) a nivel de aplicación y ofrece binarios más optimizados.
- `debug`: Ofrece lo contrario, con registros pesados y sin optimizaciones. Por esta razón, **solo debes usarlo para fines de depuración** y **cuando necesites obtener registros para crear un reporte de error (Issue)**.

En cuanto a las ramas (branches), siempre debes usar la rama `main`, a menos que los desarrolladores indiquen lo contrario, o si deseas probar próximas funciones y eres consciente de los riesgos que esto implica.

### 2. Flashea el zip

Después de elegir la compilación correcta, debes flashearla usando tu gestor de root actual, como Magisk o KernelSU. Puedes hacerlo yendo a la sección de `Módulos` de tu gestor de root y seleccionando el zip que descargaste.

Después de flashear, revisa los registros de instalación para asegurarte de que no haya errores y, si todo está bien, puedes reiniciar tu dispositivo.

> [!WARNING]
> Los usuarios de Magisk deben desactivar el Zygisk integrado, ya que entrará en conflicto con ReZygisk. Esto se puede hacer yendo a la sección `Ajustes` de Magisk y desactivando la opción `Zygisk`.

### 3. Verifica la instalación

Después de reiniciar, puedes verificar si ReZygisk está funcionando correctamente revisando la descripción del módulo en la sección `Módulos` de tu gestor de root. La descripción debería indicar que los daemons necesarios se están ejecutando. Por ejemplo, si tu entorno soporta tanto 64-bit como 32-bit, debería verse similar a esto: `[Monitor: ✅, ReZygisk 64-bit: ✅, ReZygisk 32-bit: ✅] Standalone implementation of Zygisk.`

## Traducción

Actualmente hay dos formas diferentes de contribuir con traducciones para ReZygisk:

- Para traducciones del README, puedes crear un nuevo archivo en la carpeta `READMEs`, siguiendo la convención de nomenclatura `README_<idioma>.md`, donde `<idioma>` es el código del idioma (ej. `README_es-MX.md` para Español de México), y abrir un pull request a la rama `main` con tus cambios.
- Para traducciones de la WebUI de ReZygisk, primero debes contribuir en nuestro [Crowdin](https://crowdin.com/project/rezygisk). Una vez aprobado, recupera el archivo `.json` de ahí y abre un pull request con tus cambios, agregando el archivo `.json` a la carpeta `webroot/lang` y tus créditos al archivo `TRANSLATOR.md`, en orden alfabético.

## Soporte

Para cualquier pregunta relacionada con ReZygisk u otros proyectos de PerformanC, siéntete libre de unirte a cualquiera de los siguientes canales:

- Canal de Discord: [PerformanC](https://discord.gg/uPveNfTuCJ)
- Canal de Telegram de ReZygisk: [@rezygisk](https://t.me/rezygisk)
- Canal de Telegram de PerformanC: [@performancorg](https://t.me/performancorg)
- Grupo de Signal de PerformanC: [@performanc](https://signal.group/#CjQKID3SS8N5y4lXj3VjjGxVJnzNsTIuaYZjj3i8UhipAS0gEhAedxPjT5WjbOs6FUuXptcT)

## Contribución

Es obligatorio seguir las [Pautas de Contribución](https://github.com/PerformanC/contributing) de PerformanC para contribuir a ReZygisk. Siguiendo su Política de Seguridad, Código de Conducta y estándar de sintaxis.

## Licencia

ReZygisk está licenciado mayoritariamente bajo GPL, por Dr-TSNG, pero también bajo AGPL 3.0, por The PerformanC Organization, para el código reescrito. Puedes leer más al respecto en la [Open Source Initiative](https://opensource.org/licenses/AGPL-3.0).