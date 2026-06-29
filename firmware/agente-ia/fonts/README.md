# Fuentes del firmware

## Michroma-Regular.ttf

- **Fuente:** Michroma, de Vernon Adams.
- **Licencia:** SIL Open Font License 1.1 (OFL) — libre y redistribuible (ver `OFL.txt`).
- **Origen:** Google Fonts (`ofl/michroma`).
- **Uso aquí:** alternativa **libre** de estilo cuadrado/ancho/técnico para emular
  *Eurostile / Microgramma D Extended* (propietarias) en los rótulos del tablero KITT.
- **Conversión:** se transforma a un header GFX para LovyanGFX con `tools/ttf_to_gfx.py`,
  aplicando "negrita por software". El resultado es `../font_kitt_extended.h`.

### Regenerar el header

```bash
python tools/ttf_to_gfx.py \
  --ttf firmware/agente-ia/fonts/Michroma-Regular.ttf \
  --out firmware/agente-ia/font_kitt_extended.h \
  --name KittFontExtended --size 24 --bold 1 --boldv 1
```

### Sustituir por la fuente con licencia (opcional)

Si dispones de `Microgramma D Extended.ttf` (o `Eurostile Bold Extended.ttf`) con licencia,
colócala aquí y regenera el header apuntando `--ttf` a ese archivo. No hay que tocar el firmware.
