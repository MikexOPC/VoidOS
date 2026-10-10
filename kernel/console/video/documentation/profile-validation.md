# Profilazione verificata — 10 ottobre 2026

[Design e limiti](profile-design.md). Le sei fasi sono misurate sulla stessa
cella; la somma dei loro delta viene verificata prima di aggiornare gli aggregati.
Non è stata ancora applicata un'ottimizzazione al disegno.

## Immagini e prove finali

- Diagnostica: `kernel/build/console-profile-checked/kernel.elf`, SHA256
  `FBD8A58BD6F64F4ECEE413D7539CEBC3289A24868BE2EF52355026658771EA5E`.
- Dieci boot consecutivi: `kernel/build/ps2-input-firmware-87592a550a8348ac8f0200393faa195e/`.
- Ordinaria: `kernel/build/console-profile-ordinary-final/kernel.elf`, SHA256
  `BACA817DEE5C815E815859A8298A61F9F8485CBB9580735589C2ED51E0AFFC80`.
- Dieci boot ordinari: `kernel/build/framebuffer-live-bcb7d60474284180ab034fc8d0388c6a/`.
- Dieci boot senza video: `kernel/build/framebuffer-live-144c9d5d92484d4b9e14aad41e459232/`.

30 boot finali passano. Loader preservato, nessuna installazione ESP.
Diagnostica: 3543 celle, zero perdite input osservate, 128 pixel fisici e 128
visibili finali verificati per boot, budget fisico 20438 prima e dopo.
Massimo flush 26166 tick PM = 7,310 ms; zero celle oltre l'obiettivo 10 ms
in questa campagna. Questo non dimostra la risoluzione dei picchi precedenti:
profilazione, carico host e campagne non costituiscono un confronto controllato.

| Fase | Tick aggregati | Quota |
| --- | ---: | ---: |
| Gate e preparazione | 940895 | 3,9335% |
| Font e piano testo | 138323 | 0,5783% |
| Apertura sessione e inventario | 22566722 | 94,3428% |
| Pixel e readback | 214743 | 0,8978% |
| Autorità finale | 27252 | 0,1139% |
| Ritorno gateway | 31995 | 0,1338% |

Totale 23919930 tick. Aggregati e valori per boot:
`kernel/build/console-profile-summary.json`. Non sommare massimi di fasi
diverse. Tempo host sottratto a QEMU e costo delle letture clock non separati;
nessuna misura dell'intero IF0 o della latenza end-to-end dell'input.

## Test e review

Suite `kernel/build/console-profile-host/`: C con MSVC /W4 /WX e ASan;
profiler: null/disallineamento, copie, ordine, wrap 24/32 bit, zero tick
parziale, clock fermo, soglie e overflow senza aggiornamenti parziali.
Regressioni griglia, renderer, misuratore, gateway, snapshot/stack mock e HPET
passano. Rust: 4 casi cursor e 4 timing sia debug sia release; Clippy passa.
Python: 7 casi active e 5 firmware passano, incluso rifiuto di report incompleti
o incoerenti. Un tentativo sandbox Clippy/Python ha incontrato WinError5;
le esecuzioni con accesso alla toolchain passano, senza modificare i test.

Cppcheck: zero diagnostiche error/warning/performance/portability; restano
11 style/18 information nel subset base e 28 style/29 information nel subset
active, principalmente funzioni ABI non usate nel subset e include mancanti.
Review manifest: zero errori, massimo sorgente 293 righe, massimo 5 file diretti.
ASan strumenta C host; Rust/ASM e RAM fisica non sono strumentati. Profiler
senza allocazioni dinamiche; budget QEMU verificato separatamente. Valgrind
non eseguito su Windows, coverage linee/branch N/D.

Due campagne precedenti da dieci boot sono conservate come prove intermedie:
`ps2-input-firmware-00fb269b02914dd194fe0d849bfc2c59` e
`ps2-input-firmware-99e6e203462c49b2913aec31065321d4`. Non sostituiscono i
dieci boot della versione con controllo dei puntatori disallineati.

Conclusione: la scansione completa domina il costo. L'utente ha scelto
gruppi di massimo 8 celle adiacenti sulla stessa riga, controlli completi per
gruppo, senza autorità memorizzata tra gruppi. L'implementazione è il blocco
successivo; shell ancora seriale, nessuna garanzia hardware reale/realtime.
