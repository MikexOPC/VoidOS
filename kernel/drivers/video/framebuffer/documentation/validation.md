# Framebuffer comune — validazione

I report completi precedenti sono in `kernel/drivers/video/framebuffer/documentation/` al commit `49f8cef`.
I risultati riportati riguardano le immagini dei rispettivi checkpoint.

Il checkpoint handoff v4 prova 10/10 con video, 10/10 senza
video e 10/10 regressioni PS/2. Il preflight mapping distingue
il calcolo del piano dal mapping live. I test pixel coprono
formati, stride, buffer corto e coordinate fuori range. Il
test firmware di pixel verifica le primitive del loader e la
geometria, mentre il disegno del kernel è un caso separato.

Suite host con `./kernel/drivers/video/framebuffer/scripts/test.ps1`.
Il registro include `framebuffer-draw`. I runner degli originali
conservano i comandi per probe GOP, piano e caso senza display.

[Design del modulo](design.md).

<details>
<summary>Directory delle prove storiche</summary>

```text
framebuffer-firmware-0f0b06e3f77d4acfaff703a356bc8aaf
framebuffer-firmware-316947b1070b4274aee28e86b7463c86
framebuffer-firmware-39bdd311e4f542ceaaf6b8ec56a9ae14
framebuffer-firmware-dd5fa09675694c5abd6f9a622d285f43
framebuffer-firmware-e09fbdf3b69a48e9946ac8ed1902da1e
framebuffer-firmware-f08080329b4848af81634becdf216720
framebuffer-firmware-f1da6ac7ffaa4b0180a92060dad455c4
framebuffer-firmware-f8c0c66a16e0419e90cb4ae264d0bd5b
framebuffer-firmware-fdcfc3adfc254fff8f29547e27fa888b
ps2-input-firmware-eee37fb51c80479283e67c01c6d4b0ce
```

</details>
