# ReZygisk

[English](../README.md)

ReZygisk es un fork de Zygisk Next, una implementación independiente de Zygisk, que provee soporte para la API de Zygisk en KernelSU, APatch y Magisk.

El código base fue reescrito completamente en C, lo que no solo trae un código mucho más limpio y fácil de seguir, sino también binarios más livianos y rápidos. También se introdujeron linkers personalizados para proteger a ReZygisk de detecciones futuras, sin usar el linker del sistema bajo circunstancias normales y evadiendo así cualquier detección basada en linkers.

## ¿Por qué?

Las últimas versiones de Zygisk Next no son de código abierto (open-source), reservando todo el código para sus desarrolladores. Esto no solo limita nuestra capacidad de aportar al proyecto, sino que también imposibilita la auditoría del código. Esto es un problema de seguridad importante, ya que Zygisk Next es un módulo que corre con privilegios de superusuario (root) y tiene acceso a todo el sistema.

Los desarrolladores de Zygisk Next son conocidos y de confianza en la comunidad de Android; sin embargo, esto no significa que el código no sea malicioso o vulnerable. Nosotros (PerformanC) entendemos que tienen sus razones para mantener el código cerrado, pero creemos en lo contrario.

## Ventajas

- FOSS (Para siempre)

## Dependencias

| Herramienta     | Descripción                            |
|-----------------|----------------------------------------|
| `Android NDK`   | Kit de desarrollo nativo para Android  |

### Dependencias de C

| Dependencia | Descripción                            |
|-------------|----------------------------------------|
| `PLTI`      | PLT Hook simple para Android           |
| `CSOLoader` | Linker personalizado SOTA para Linux   |

## Instalación

### 1. Elegí el zip correcto

La elección de la build/zip es importante, ya que va a determinar qué tan oculto y estable sea ReZygisk. Igual, esto no es una tarea difícil:

- `release` debería ser la elegida para la mayoría de los casos; saca el registro de logs a nivel de aplicación y ofrece binarios más optimizados.
- `debug`, por otro lado, ofrece lo contrario: muchos logs y cero optimizaciones. Por este motivo, **solo deberías usarlo para debugging (depuración)** y **cuando necesites sacar logs para abrir un Issue**.

En cuanto a las ramas (branches), siempre deberías usar la rama `main`, a menos que los desarrolladores te digan lo contrario, o si querés probar funciones nuevas y sos consciente de los riesgos que implica.

### 2. Flasheá el zip

Después de elegir la build correcta, tendrías que flashearla usando tu gestor de root actual, como Magisk o KernelSU. Podés hacer esto yendo a la sección de `Módulos` (`Modules`) de tu gestor de root y seleccionando el zip que te bajaste.

Después de flashear, revisá los logs de instalación para asegurarte de que no haya errores. Si está todo bien, podés reiniciar tu dispositivo.

> [!WARNING]
> Los usuarios de Magisk deben desactivar el Zygisk que viene integrado, ya que va a entrar en conflicto con ReZygisk. Podés hacerlo yendo a la sección de `Configuración` (`Settings`) en Magisk y desactivando la opción `Zygisk`.

### 3. Verificá la instalación

Después de reiniciar, podés verificar si ReZygisk está funcionando bien revisando la descripción del módulo en la sección de `Módulos` de tu gestor de root. La descripción debería indicar que los demonios (daemons) necesarios están corriendo. Por ejemplo, si tu entorno soporta tanto 64-bit como 32-bit, debería verse parecido a esto: `[Monitor: ✅, ReZygisk 64-bit: ✅, ReZygisk 32-bit: ✅] Standalone implementation of Zygisk.`

## Traducción

Actualmente hay dos maneras diferentes de aportar con traducciones para ReZygisk:

- Para traducir el README, podés crear un archivo nuevo en la carpeta `READMEs`, siguiendo el formato de nombre `README_<idioma>.md`, donde `<idioma>` es el código del idioma (por ejemplo, `README_es-AR.md` para Español de Argentina), y abrir un pull request a la rama `main` con tus cambios.
- Para traducir la WebUI de ReZygisk, primero deberías aportar a nuestro [Crowdin](https://crowdin.com/project/rezygisk). Una vez aprobado, bajate el archivo `.json` de ahí y abrí un pull request con tus cambios (agregando el archivo `.json` a la carpeta `webroot/lang` y tus créditos al archivo `TRANSLATOR.md`, en orden alfabético).

## Soporte

Por cualquier duda relacionada con ReZygisk u otros proyectos de PerformanC, sentite libre de sumarte a cualquiera de los siguientes canales:

- Canal de Discord: [PerformanC](https://discord.gg/uPveNfTuCJ)
- Canal de Telegram de ReZygisk: [@rezygisk](https://t.me/rezygisk)
- Canal de Telegram de PerformanC: [@performancorg](https://t.me/performancorg)
- Grupo de Signal de PerformanC: [@performanc](https://signal.group/#CjQKID3SS8N5y4lXj3VjjGxVJnzNsTIuaYZjj3i8UhipAS0gEhAedxPjT5WjbOs6FUuXptcT)

## Contribución

Es obligatorio seguir las [Guías de Contribución](https://github.com/PerformanC/contributing) de PerformanC para aportar a ReZygisk. Esto incluye respetar su Política de Seguridad, su Código de Conducta y el estándar de sintaxis.

## Licencia

ReZygisk está bajo la licencia [AGPL 3.0](./LICENSE). Podés leer más sobre esto en la [Open Source Initiative](https://opensource.org/licenses/AGPL-3.0).
