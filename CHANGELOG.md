# Changelog

Cambios de cada versión publicada de RdPiano. La más nueva, arriba.

## 1.0.0 — 1 de septiembre de 2026

Primera versión con número propio. El plugin ya se puede usar en un concierto.

### Sonido

- Los dieciséis sonidos suenan ahora al mismo nivel: antes unos se quedaban
  cortos y otros saturaban al cambiar de uno a otro.
- El chorus y el phaser ya no sueltan un ruido con lo último que sonó cuando se
  los enciende después de un rato apagados.
- Encender o apagar un efecto se hace con una transición suave, sin salto.
- Mover el volumen o cambiar de sonido ya no produce el zumbido de antes.

### Uso en directo

- El dial de sonidos enseña el nombre mientras se gira y cambia el sonido al
  soltarlo, así no se pasa por los quince de en medio.
- El plugin le dice al programa anfitrión cuánto tarda en responder, para que
  la grabación quede en su sitio.
- Los ajustes guardados recuperan todo, incluidos el sonido elegido y la
  afinación.

### Instalación

- Cinco formatos: AU, AUv3, VST3, LV2 y aplicación independiente.
- Un solo archivo para Mac con procesador Apple y para Mac con Intel.
- Requiere macOS 11 o posterior.
- El archivo LV2 pasa a llamarse `rdpiano_juce.lv2`. Quien tuviera instalada
  una versión anterior puede borrar el `RdPiano.lv2` que quede suelto.

### Por dentro

- El plugin gasta bastante menos memoria y ya no la va perdiendo con el uso.
- Se compila con una sola orden, y hay pruebas automáticas que comprueban en
  cada cambio que el sonido sigue siendo exactamente el mismo.
