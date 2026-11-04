# Integraciones oficiales: Twitch y Kick

Stream Planner puede iniciar sesión con OAuth y publicar el título, la categoría y
las etiquetas del canal usando las APIs oficiales. La notificación a la comunidad
sigue siendo local (se copia), porque ninguna de las dos APIs permite publicarla.

## Qué permite cada plataforma

| Acción | Twitch | Kick |
| --- | --- | --- |
| Iniciar sesión | Device Code Flow | Authorization Code + PKCE |
| Cambiar título | Sí (máx. 140) | Sí (máx. 100 en la app) |
| Cambiar categoría | Sí (búsqueda + scroll) | Sí (búsqueda + scroll) |
| Leer etiquetas | Sí | Sí (`stream.custom_tags`) |
| Añadir / borrar etiquetas | Sí (máx. 10, 25 caracteres) | Sí (máx. 10) |
| Publicar notificación | No | No |

## Dónde se guarda la configuración

| Sistema | Configuración | Tokens OAuth |
| --- | --- | --- |
| Linux | `~/.config/StreamPlanner/integrations.json` | `~/.local/share/StreamPlanner/tokens.json` |
| Flatpak | `~/.var/app/io.github.mestisexo.StreamPlanner/config/StreamPlanner/integrations.json` | `~/.var/app/io.github.mestisexo.StreamPlanner/data/StreamPlanner/tokens.json` |
| Windows | `%APPDATA%\StreamPlanner\integrations.json` | `%LOCALAPPDATA%\StreamPlanner\tokens.json` |

Variables de entorno opcionales:

- `STREAMPLANNER_CONFIG`: ruta explícita de `integrations.json`.
- `STREAMPLANNER_TOKENS`: ruta explícita de `tokens.json`.
- `SP_TWITCH_CLIENT_ID`, `SP_TWITCH_CLIENT_SECRET`.
- `SP_KICK_CLIENT_ID`, `SP_KICK_CLIENT_SECRET`, `SP_KICK_REDIRECT_URI`.

Las variables de entorno tienen prioridad sobre el archivo.

## Registro y verificación de credenciales

Cada usuario debe registrar sus propias credenciales de aplicación. La primera vez
que se abre la app aparece un diálogo que las pide y las verifica **antes** de
guardarlas. Si la verificación falla, la interfaz principal no se abre.

- **Twitch** se valida pidiendo un *device code* a
  `https://id.twitch.tv/oauth2/device`. Solo se necesita el Client ID.
- **Kick** se valida pidiendo un *app access token* con
  `grant_type=client_credentials` a `https://id.kick.com/oauth/token`. Hace falta
  Client ID y Client Secret.
- Basta con configurar una plataforma; si se empieza a rellenar la otra, hay que
  terminarla y verificarla también.
- Al guardar se escribe `"verified": true` y una huella SHA-256 de las
  credenciales (`credential_hash`). Si después se edita cualquier credencial, la
  huella deja de coincidir y la app vuelve a pedir la verificación.
- Para cambiar las credenciales con la app abierta:
  - **Qt6:** botón **Credenciales...** en la pestaña **Plataformas**.
  - **Win32:** menú **Integraciones → Credenciales...**.

El diálogo también permite cambiar la *Redirect URI* de Kick. Debe coincidir
exactamente con la registrada en la app de Kick.

## Crear la aplicación de Twitch

1. Entra en <https://dev.twitch.tv/console/apps> y crea una aplicación.
2. Tipo de cliente: **Public** (no se usa client secret para el Device Code Flow).
3. Como *OAuth Redirect URL* puedes indicar `http://localhost` (el flujo de
   dispositivo no redirige a esa URL, pero la consola la exige).
4. Copia el **Client ID** en el diálogo de credenciales (o en `integrations.json`).
5. El scope que solicita la app es `channel:manage:broadcast`.

Twitch abrirá el navegador y mostrará un código de activación. La app sondea la
autorización hasta que la aceptes o caduque.

## Crear la aplicación de Kick

1. Entra en <https://dev.kick.com> y crea una aplicación.
2. Añade como *Redirect URL*: `http://127.0.0.1:7512/callback`
   (la comparación es exacta; si cambias el puerto, cámbialo en los dos sitios).
3. Copia **Client ID** y **Client Secret** en el diálogo de credenciales (o en
   `integrations.json`).
4. Scopes solicitados: `user:read channel:read channel:write`.

Kick exige `client_secret` incluso con PKCE. El secreto se guarda solo en tu
configuración local y nunca se incluye en el repositorio.

## Ejemplo de `integrations.json`

Copia [`config/integrations.example.json`](../config/integrations.example.json) a
la ruta de configuración de tu sistema y sustituye los valores:

```json
{
  "twitch": {
    "client_id": "tu_client_id_de_twitch",
    "client_secret": "",
    "redirect_uri": ""
  },
  "kick": {
    "client_id": "tu_client_id_de_kick",
    "client_secret": "tu_client_secret_de_kick",
    "redirect_uri": "http://127.0.0.1:7512/callback"
  }
}
```

Los valores que empiezan por `GENERIC_` se consideran placeholders y dejan la
integración desactivada con un mensaje claro en la interfaz.

Tras verificarlas desde la app, el archivo incluye además:

```json
{
  "verified": true,
  "credential_hash": "<huella SHA-256 de las credenciales>"
}
```

No edites esos dos campos a mano: se recalculan al verificar y cualquier cambio en
las credenciales invalida la marca.

## Uso

- **Qt6 (Linux):** pestaña **Plataformas** del panel derecho. Elige Twitch o Kick,
  pulsa **Conectar**, edita el título, busca y selecciona la categoría, añade o
  quita etiquetas y pulsa **Publicar**. El botón **Credenciales...** vuelve a abrir
  el registro de credenciales.
- **Win32 (Windows):** botón **Publicar en plataformas...**. El diálogo permite
  cambiar de plataforma, reconectar, leer el canal, buscar categorías y publicar.
  El menú **Integraciones → Credenciales...** permite cambiar las credenciales.
- El estado por plataforma (título, categoría, etiquetas y último error) se guarda
  en la base de datos, no en el archivo de tokens.

## Seguridad

- `integrations.json` contiene el Client Secret de Kick. En Linux se escribe con
  permisos `0600` y de forma atómica; en Windows hereda la ACL del perfil del
  usuario. No se usa llavero del sistema ni cifrado adicional.
- Los tokens se guardan en un JSON aparte. En Linux se escriben con permisos
  `0600` y de forma atómica. En Windows heredan la ACL del perfil del usuario.
- `integrations.json` y `tokens.json` nunca deben compartirse ni versionarse.
  `.gitignore` ya excluye las copias locales de `config/`.
- **Desconectar** borra los tokens locales de esa plataforma. Para revocar el
  acceso también puedes hacerlo desde la web de Twitch/Kick.

## Red y Flatpak

Las integraciones necesitan HTTPS saliente y, en Kick, un servidor loopback en
`127.0.0.1`. El manifiesto Flatpak incluye `--share=network` por este motivo. En
AppImage o paquetes de distro, asegúrate de que el backend TLS de Qt (OpenSSL 3)
está disponible.

## Problemas frecuentes

- **«Falta ... client_id»**: el archivo no está en la ruta esperada o sigue con
  el placeholder `GENERIC_...`.
- **La verificación falla con «invalid client»**: el Client ID no pertenece a una
  app existente o (en Kick) el Client Secret no coincide. Revisa ambos valores en
  el portal de desarrollador.
- **Kick no puede escuchar en el puerto**: otro proceso usa el 7512. Cambia el
  puerto en `integrations.json` y en la Redirect URL de la app de Kick.
- **401/403**: vuelve a conectar para renovar el token o revisa que la app tenga
  los scopes indicados.
- **429**: límite de peticiones de la plataforma; espera unos segundos.
- **Etiquetas vacías en Kick**: la API solo devuelve `custom_tags` dentro del
  objeto `stream`; si el canal está offline puede no haber datos.
