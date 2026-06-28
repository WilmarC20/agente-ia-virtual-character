# LinkedIn — Guion final (publicar)

**Artículo #1:** https://www.linkedin.com/pulse/constru%C3%AD-un-sistema-que-escucha-entiende-y-act%C3%BAa-esto-mahpe/

---

## Cómo montarlo en LinkedIn

| Slide | Imagen | Cuándo la menciona el texto |
|-------|--------|-----------------------------|
| **1** | Portada héroe (conceptual, épica, puede verse “producida”) | Hook visual al abrir |
| **2** | Neutral — primer plano real | Cabeza 3D, 12 caras |
| **3** | Enojado — primer plano real | Rate limit, Bender avisa |
| **4** | Vibing — primer plano real | Personalidad, boca con música |
| **5** | Guiño — primer plano real | Cierre, ESP-CAM próxima |

**Formato:** post del feed con **carrusel de 5 imágenes**.

---

## Título (primera línea)

**Prometí Parakeet en vivo para un call center. Terminé con un robot que me grita cuando Cursor se traba.**

---

## Post principal (copiar y pegar)

En junio escribí sobre STT en producción: audio feo, latencia que se siente, contact center, palabras gatillo.

Comparé **Whisper** (preciso, acentos) con **Parakeet-TDT** (NVIDIA, streaming, mucho más rápido). Conclusión: en demo todo brilla; en producción el mic te come vivo.

Cerré el artículo **prometiendo la demo** — transcripción en vivo, estilo call center, con ese stack.

Tres meses después: la demo sigue en el horno. Lo que sí salió del horno fue una cabeza impresa en 3D con **12 caras**, voz de Bender y una boca que baila reggaetón como si cobrara por eso.

¿Whisper y Parakeet siguen corriendo en mi GPU? Sí.

¿La demo del call center? **Próximamente.** ™

¿Visión? **Cero.** Oye, piensa, habla, hace muecas… y no ve una pared. La **ESP-CAM** entra la semana que viene. Prioridades de proyecto con déficit de foco.

Lo que no esperaba — y es lo más útil — son los **hooks de Cursor y Claude**:

El agente se traba, pide permiso, o Anthropic te corta el rate limit… y en vez de un *ding* de Windows, suena **Bender desde la mesa**:

> *"¡Claude se ahogó de pedidos! Anthropic le cerró la llave, bolsa de carne. Tomá un mate y reintentá."*

Cara de enojo. Anoche pasó en serio.

`POST /api/dev/notify` → voz + emoción en pantalla:

→ Cursor pregunta algo  
→ Claude pide aprobación  
→ Una tool falla  
→ Un subagente termina  

Laburo con el IDE minimizado. El robot es mi **"che, volvé al teclado"** — con actitud de robot de desguace.

El video va a arrancar pidiendo perdón por no mostrar Parakeet en el call center todavía.

Y va a cerrar con Cursor rompiendo algo en vivo para que Bender me hable en directo.

¿Tu agente te avisa cuando se traba, o lo descubrís media hora después en una pestaña olvidada?

Parte 1 → https://www.linkedin.com/pulse/constru%C3%AD-un-sistema-que-escucha-entiende-y-act%C3%BAa-esto-mahpe/

#IA #Cursor #Claude #ESP32 #VoiceAI #Maker

---

## Posts de seguimiento

**D+2**
```
Cursor + Claude → webhook → robot en la mesa.

Anoche: rate limit de Anthropic.
Bender, enojado: "bolsa de carne, tomá un mate."

Todavía no ve nada. ESP-CAM en camino.
Por ahora solo te avisa cuando no lo estás mirando.
(Casi siempre.)
```

**D+5 — ESP-CAM**
```
Update honesto:

✓ Escucha
✓ Habla
✓ Insulta (con estilo Bender)
✓ Se conecta a Cursor y Claude
✗ Ve

ESP-CAM: esta semana.
Demo call center Parakeet: sigue en "próximamente". ™
```

---

## Apertura video (20 s)

```
Junio: Whisper vs Parakeet, call center en vivo. Prometí esa demo.

Hoy: robot que oye, habla, tiene cara…
y no distingue un mate de una pared.

ESP-CAM: la semana que viene.

Pero ya te avisa cuando Cursor o Claude se traba.

[Bender habla en vivo]

Esa es la demo. Por ahora.
```
