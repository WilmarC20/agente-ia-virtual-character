# RESTORE — Cómo volver al estado estable

Este proyecto tiene un **punto de restauración** que representa el último estado
estable antes de la nueva arquitectura *Aura Engine*:

- **Tag:** `v1.0.0-foundation`
- **Rama estable:** `main`
- **Remoto:** `origin` → `https://github.com/WilmarC20/agente-ia-virtual-character.git`

> Todos los comandos se ejecutan desde la raíz del repositorio.
> Antes de restaurar, **guarda tu trabajo** (`git stash` o un commit), porque las
> operaciones de restauración pueden descartar cambios sin confirmar.

---

## 0. Ver qué hay disponible

```bash
git fetch --all --tags        # traer ramas y tags del remoto
git tag --list                # ver tags (debe aparecer v1.0.0-foundation)
git branch -a                 # ver ramas locales y remotas
git log --oneline -10         # commits recientes
```

---

## 1. Restaurar usando el TAG (recomendado)

El tag es un ancla inmutable al estado estable.

**Inspeccionar el estado del tag sin modificar ramas** (modo "detached HEAD"):

```bash
git checkout v1.0.0-foundation
```

**Crear una rama de trabajo a partir del tag** (para seguir trabajando):

```bash
git checkout -b rescate-estable v1.0.0-foundation
```

**Devolver `main` exactamente al tag** (⚠️ reescribe `main` local):

```bash
git checkout main
git reset --hard v1.0.0-foundation
```

---

## 2. Restaurar usando un COMMIT

Con el hash del commit del checkpoint (lo obtienes con `git log` o `git show v1.0.0-foundation`):

```bash
git checkout <hash-del-commit>            # inspeccionar (detached HEAD)
git checkout -b rescate <hash-del-commit> # crear rama desde ese commit
git reset --hard <hash-del-commit>        # ⚠️ mover la rama actual a ese commit
```

Para **deshacer un commit** sin perder los cambios (los deja sin confirmar):

```bash
git reset --soft HEAD~1
```

---

## 3. Restaurar usando la RAMA

Volver a la rama estable:

```bash
git checkout main
git pull origin main          # sincronizar con el remoto
```

Si `main` local quedó dañada y quieres igualarla al remoto (⚠️ descarta cambios locales):

```bash
git fetch origin
git checkout main
git reset --hard origin/main
```

---

## 4. Restaurar un ARCHIVO específico

Recuperar un solo archivo desde el tag (o desde un commit/rama):

```bash
git checkout v1.0.0-foundation -- ruta/al/archivo.ext
# ejemplos:
git checkout v1.0.0-foundation -- firmware/agente-ia/face.h
git checkout main             -- server/main.py
```

Descartar cambios sin confirmar de un archivo (volver al último commit):

```bash
git restore ruta/al/archivo.ext
```

---

## 5. Cancelar la refactorización (abandonar Aura Engine)

Si estás trabajando en `feature/aura-engine` y quieres abortar y volver a estable:

```bash
git checkout main             # volver a la rama estable
# (opcional) borrar la rama de refactor local y remota:
git branch -D feature/aura-engine
git push origin --delete feature/aura-engine
```

Tus cambios de la refactor quedan descartados solo si **no** hiciste merge a `main`.
Mientras no se haga merge, `main` y el tag permanecen intactos.

---

## 6. Regresar COMPLETAMENTE al estado del checkpoint

Restauración total (⚠️ descarta TODO lo no confirmado y reescribe `main` local):

```bash
git fetch --all --tags
git checkout main
git reset --hard v1.0.0-foundation
git clean -fd                 # ⚠️ borra archivos sin seguimiento (revisa antes con: git clean -nd)
```

> `git clean -fd` **no** borra lo que está en `.gitignore` por defecto. Los binarios
> grandes (builds, modelos RVC, voces) no se versionan: tras restaurar el código,
> recompila el firmware y reinstala dependencias del servidor para regenerarlos.

---

## 7. Recompilar tras restaurar

- **Servidor:** `cd server` → `./start.ps1` (crea `.venv`, instala, levanta uvicorn).
- **Firmware:** abrir `firmware/agente-ia/agente-ia.ino` en Arduino IDE
  (ESP32S3 Dev Module, OPI PSRAM, 16MB, partición ESP SR 16M) o con `arduino-cli`.
  Si el enlazado falla por `.debug_line`/ICE, compilar limpio y en serie:
  `arduino-cli compile --clean --jobs 1 ...`.
- **Secretos:** recrear `firmware/agente-ia/secrets.h` y `server/secrets.local.ps1`
  a partir de las plantillas `*.example`.
