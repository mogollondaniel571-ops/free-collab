# Free Collab v0.2

Mod de Geode para Geometry Dash: hasta **6 personas** editando el mismo nivel a la vez, con
invitaciones, cursores de los demás y cambios en tiempo real. Gratis.

> Esta versión **no se pudo probar dentro del juego** (se escribió contra las funciones reales del SDK
> de Geode, pero sin poder ejecutarlo). Es normal que la primera compilación dé algún error o que algo
> necesite ajustes: copia el mensaje completo y pídele a Claude que lo arregle.

## Cómo funciona

- Un **servidor** muy liviano (`server/server.js`) conecta a todos. Solo reenvía mensajes de texto,
  casi no gasta procesador, así que no hace lag en la compu donde corra.
- Todos los que abran el juego con el mod se conectan al servidor. La lista de "amigos" que ves son
  **los que están conectados a ese mismo servidor**.
- En **Mis niveles** → tu nivel → botón **Colab** (abajo a la izquierda): ves a los conectados e invitas.
- El invitado recibe un aviso, pulsa **Entrar** y abre el editor dentro del nivel.
- Si el anfitrión se sale, **los demás siguen**: el servidor guarda el nivel y los cambios.

## Parte 1 (opción fácil): compilar en la nube con GitHub, sin instalar nada

1. Crea una cuenta gratis en https://github.com y un repositorio nuevo (público es lo más simple).
2. Descomprime el zip y sube **el contenido** de la carpeta `free-collab` (no la carpeta en sí:
   `mod.json` debe quedar en la raíz). Incluye la carpeta oculta `.github`; si no se sube al arrastrar,
   en GitHub usa **Add file → Create new file**, escribe el nombre `.github/workflows/build.yml` y pega
   el contenido de ese archivo.
3. Entra a la pestaña **Actions**. La compilación empieza sola y tarda unos 10 a 20 minutos.
4. Cuando termine con ✅, abre esa ejecución, baja a **Artifacts** y descarga **FreeCollab**.
   Adentro está el archivo `.geode`: cópialo a la carpeta `geode/mods` de tu Geometry Dash.
5. Si termina con ❌: abre la ejecución, copia el texto rojo del error y pásaselo a Claude.

## Parte 1 (opción en tu PC): compilar el mod

Solo si prefieres compilar tú mismo.

Guías oficiales: https://docs.geode-sdk.org/getting-started/cpp-stuff y
https://docs.geode-sdk.org/getting-started/geode-cli

1. Instala estos programas (en este orden):
   - **CMake** (versión 3.29 o más nueva). Marca la opción de agregarlo al PATH.
   - **Git**.
   - **Scoop** (https://scoop.sh) y luego, en PowerShell: `scoop install llvm ninja`
   - **Build Tools for Visual Studio** (descárgalo al final de la página de descargas de Visual Studio).
     Marca **"Desarrollo para el escritorio con C++"**.
2. Instala el Geode CLI. En PowerShell:

       winget install GeodeSDK.GeodeCLI

   Cierra y abre la terminal, y comprueba con `geode --version`.
3. Prepara el SDK (una sola vez):

       geode config setup
       geode sdk install
       geode sdk install-binaries

   Cierra y abre la terminal otra vez.
4. Abre una terminal dentro de esta carpeta y ejecuta:

       geode build

5. Se crea un archivo `.geode`. Cópialo a la carpeta `geode/mods` de tu Geometry Dash.
   Todos los que quieran colaborar necesitan **el mismo archivo**.

Si sale un error que dice *"Please change the Geode version in your mod.json"*, abre `mod.json` y cambia
la línea `"geode"` por la versión que dice el error.

## Parte 2: el servidor

Hace falta **Node.js** (https://nodejs.org) en UNA computadora (la que haga de servidor).

    cd server
    node server.js

Queda escuchando en el puerto 7777. Opciones para que tus amigos lleguen a él:

- **Misma casa o red:** usan la IP local de esa compu como "Servidor (host)".
- **Por internet, gratis:** un túnel TCP como **playit.gg** (https://playit.gg). Crea un túnel TCP al
  puerto 7777 y te da una dirección y un puerto: esos son el host y el puerto del mod.
- **Que no dependa de tu PC:** súbelo a cualquier servidor que corra Node.js y acepte conexiones TCP
  (por ejemplo la capa gratuita de Oracle Cloud). Los planes gratis cambian con el tiempo, revisa cuál sigue vigente.

### Atajos para Windows

- `iniciar-servidor.bat`: abre el servidor con doble clic (necesita Node.js).
- `instalar-mod.bat`: para tus amigos. Ponlo en una carpeta junto al archivo `.geode` y que le den doble
  clic. Copia el mod a su Geometry Dash. Si no encuentra el juego, le pide la carpeta.

### Para que tus amigos no tengan que configurar el servidor

Antes de compilar, abre `mod.json` y cambia los valores `"default"` de **server-host** y **server-port**
por los de tu servidor. Así el mod ya sale configurado y cada amigo solo pone su nombre.
Si alguien no pone nombre, el mod le asigna uno único (por ejemplo `jugador482`).

### Importante: versión de Geometry Dash

El mod debe compilarse para la versión de Geometry Dash que usen. Si Geode se actualiza (por ejemplo
cuando arreglen la 2.2091), vuelve a compilar con GitHub y reparte el archivo nuevo.

## Parte 3: usarlo

1. En Geode, abre los **ajustes de Free Collab** y pon: host, puerto y **Tu nombre** (sin espacios y
   distinto al de los demás).
2. Abre el juego. Verás el aviso *"conectado como ..."*.
3. **Anfitrión:** Mis niveles → tu nivel → **Colab** → **Invitar** a quien quieras → pulsa **Editar**
   como siempre.
4. **Invitado:** le llega un aviso, pulsa **Entrar**. Se abre el editor con una **copia** del nivel que
   queda guardada en tus niveles con `[collab]` al final.
5. Si te sales y quieres volver: **Colab** → **Volver a la sala**.

## Qué se sincroniza

Todo se envía como el texto interno del objeto, así que funciona con **cualquier bloque, decoración
y trigger del juego**: colocar, borrar, mover, rotar, escalar, colores, grupos y propiedades de triggers.
Además verás un cursor con el nombre de cada persona.

## Limitaciones conocidas

- Solo Windows.
- Los cambios en objetos se detectan **mientras están seleccionados**. Deshacer/rehacer (Ctrl+Z) puede
  no sincronizarse en algunos casos.
- Si dos personas editan el mismo objeto a la vez, gana el último cambio.
- Los cambios de los demás **no se guardan en el archivo del anfitrión** si él se sale: para seguir,
  el anfitrión usa **Volver a la sala** y trabaja en la copia `[collab]`.
- No se sincronizan los ajustes generales del nivel (colores de fondo, velocidad, canciones).
- "Amigos" = conectados al mismo servidor (no usa la lista de amigos de Geometry Dash).

## Ideas para la próxima versión

1. Sincronizar los ajustes del nivel
2. Deshacer/rehacer entre personas
3. Chat y colores por persona
4. Lista de amigos reales y soporte para Android
