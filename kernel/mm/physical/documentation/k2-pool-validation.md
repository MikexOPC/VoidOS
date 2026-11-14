# Allocatore fisico — validazione

I report completi precedenti sono in `kernel/mm/physical/documentation/` al commit `49f8cef`.
I risultati riportati riguardano le immagini dei rispettivi checkpoint.

Il checkpoint K2 riporta 39/39 suite host in aggregato. La prima
corsa ne passa 36. Tre errori di link dei mock vengono corretti e
rieseguiti. L'allocatore finale esegue dieci ripetizioni ASan e
14.000 cicli lease su gruppi di 2, 4 e 8 thread host. Pin e budget
tornano al valore iniziale. I mock del bootstrap coprono 22 guasti
iniettati e mantengono separata la verifica del mapper reale.

Le regressioni QEMU riportano ordinario e console GOP/VirtIO
sulle immagini del checkpoint. La lettura concorrente di
generazione è coperta da verifiche host mirate. I test ASan
non misurano la RAM fisica del guest.

[Design del modulo](k2-design.md).

<details>
<summary>Directory delle prove storiche</summary>

```text
bootstrap-host-9c41a32f03e04053b459b8398a27d1b6
physical-host-91384c769ba540f4bbadbb33f7e1fe77
physical-host-ad75bf27969543a9a8c1ebe426aefc91
pointer-interactive-check-64f89a9b736b478899dfa77df372c66a
pointer-interactive-check-875e081e1aa24498947ea4b5b9043bf9
qemu-run-14310607f3ab43058798a2efbfa8a6c9
qemu-run-6451836fdf474bf4b93ebcfbe777c097
qemu-run-97ad12d43b5d4655b9ca51271118af43
qemu-run-9fbeed6229254be4a9d839a55b2d65cc
qemu-run-e581da745cf245b08e05e2e0c319907a
sync-firmware-a5e8e803baa14afda7f37ad7d30c2951
sync-firmware-daaea37bce694cb0af48561e439010fa
sync-firmware-fa90cc7006e74e2ca2726bd260a43087
test-run-0d589d4ad49e45d68a9c2ebb39efcce9
test-run-11b054618e7643d0b4bc1605ea0f815b
test-run-3993ab5b8ce741a9aa03338d1be7a66a
test-run-5340bcd92a3c4e65a8367284d3e9c1f5
test-run-5a60d64565a746d396bb75c6704bde59
test-run-8fcfe6b023714ae89ba596942b4ca7c7
test-run-91f2cc5321a74da88a8e915a6243dd34
test-run-ad849029d99d475783c35ff5f7e42527
test-run-c840d28374c2481090c5a43b383df497
```

</details>
