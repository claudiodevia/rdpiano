# Changelog

Cambios de cada versión publicada de RdPiano. La más nueva, arriba.

## 1.0.3 — 15 de octubre de 2026

El sonido no cambia respecto a la 1.0.2.

### Manejo

- Al grabar automatización en el DAW con el modo *touch* o *latch*, ahora quedan
  grabados los movimientos del fader de volumen, de los botones de efectos y del
  dial al ajustar un efecto.
- El dial reparte por igual los quince pasos de cada ajuste de efecto: el último
  ya no exige llevarlo hasta el tope, y cada posición vuelve a dar siempre el
  mismo paso.
- Si se pulsa un sonido justo después de cambiar a otro desde el teclado o desde
  una pista, la pulsación ya no se pierde.
- El DAW también se entera cuando el sonido se cambia desde el panel.
- Un proyecto guardado con una afinación dañada ya no se abre con el
  instrumento muy desafinado hacia el lado contrario.
- Una nota que se suelta justo cuando el DAW pide un trozo de audio diminuto ya
  no se queda pulsada por dentro, y no vuelve a sonar al cambiar de sonido.
- Con el DAW a frecuencias de muestreo bajas y trozos de audio muy grandes, el
  clave, los dos clavis, el E-Piano 2 y el Contemporary ya no salen mudos.

### Por dentro

- El panel está reorganizado por piezas y su pantalla tiene pruebas
  automáticas propias. Se ve igual que antes.
- El motor de sonido está reorganizado por piezas, cada una con una sola tarea,
  y suena exactamente igual que antes. Abrir el instrumento es algo más rápido.
- El emulador del piano original está ordenado y mejor explicado por dentro, y
  suena exactamente igual que antes.
- El ecualizador de la salida y la afinación tienen pruebas automáticas
  propias; la de la afinación comprueba uno por uno todos sus valores posibles.
- Más pruebas automáticas: ahora comprueban también que la cadena de sonido
  completa —efectos, cambios de sonido y de afinación incluidos— sigue dando
  exactamente el mismo resultado, así que un cambio futuro no puede alterar el
  sonido sin que se note.
- La herramienta para escuchar el emulador sin plugin arranca como el plugin
  y suena como él.
- La descarga de lo necesario para compilar se comprueba, para que no se cuele
  un archivo dañado o alterado.

### Documentación

- La documentación técnica está reorganizada y es más breve.

## 1.0.2 — 12 de septiembre de 2026

### Sonido

- Tocar la misma tecla dos veces seguidas sin soltarla, algo habitual con notas
  que se pisan en una pista, ya no deja una nota sonando para siempre.

### Uso en directo

- Ya se puede cambiar de sonido desde el teclado o desde una pista, sin tocar la
  pantalla. Si llegan varios cambios seguidos, solo se aplica el último.

### Documentación

- Instrucciones de instalación más claras y paso a paso, pensadas para quien no
  tiene experiencia técnica.

## 1.0.1 — 4 de septiembre de 2026

### Sonido

- Cambiar de sonido mientras se toca ya no corta la música: las notas que
  estuvieran sonando y el pedal siguen adelante con el sonido nuevo, y
  reaparecen al volumen al que iban, sin el golpe de un ataque que nadie ha
  tocado.
- La transición del cambio es más suave y algo más larga, así que no se oye ni
  el corte del sonido anterior ni la entrada del siguiente.
- Cambiar la afinación ya no deja el instrumento mudo: entra con esa misma
  transición suave y lo que estuvieras tocando —notas y pedal— sigue sonando, ya
  afinado.

### Uso en directo

- El dial de afinación enseña los Hz mientras se gira y afina al soltarlo, igual
  que el de sonidos.

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
