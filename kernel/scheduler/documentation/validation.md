# Scheduler — validazione

I report completi precedenti sono in `kernel/scheduler/documentation/` al commit `49f8cef`.
I risultati riportati riguardano le immagini dei rispettivi checkpoint.

Il core cooperativo ha 13 test Rust per ciascun livello di
ottimizzazione e 486.644 controlli ABI C/Rust in host. La contesa K4-C
su quattro thread host esegue 20.000 task per run. Le campagne BSP/AP
distinguono timer periodico, frame invariati e arresti attesi.

`ap-task-four` prova otto task su quattro AP più il BSP supervisore.
Nel gate finale 10/10, ogni boot registra 64 preemption e raccolta
degli stack. `ap-task-concurrent-stress` passa 10/10, con task su
due AP e round PING/shootdown concorrenti. La matrice di chiusura
mantiene separati i 50 positivi e i 20 negativi per quiescenza
incompleta. `paging-tamper` passa 10/10 arresti attesi.

Nel profilo `v2c`, 10/10 a quattro CPU verificano il comando
`ap 1000`, somma 500500, risveglio della shell, AP1 in HLT e
budget di 14.093 pagine ripristinato. Tre shootdown per teardown.
Questi risultati QEMU non estendono la preemption alla build ordinaria.

Riproduzione nel registro con `ap-task-four`,
`ap-task-concurrent-stress`, `meson-bsp-periodic` e `v2c`.

[Design del modulo](design.md).

<details>
<summary>Directory delle prove storiche</summary>

```text
ap-console-27ebbb287c8140fd9e964184191e8581
ap-console-29ddc6cd24b54f6e9717006454748e61
ap-console-59df01d77f3240af9f060edb5e7a598e
ap-preempt-service-final-b8a5d1b2ac7b4b53854187186d5498bc
ap-runtime-05f9aac71ab04ad7afcb2020d8c9d988
ap-runtime-3a8b897886ef4382a6e938d46d7707c1
ap-runtime-4de40aa6350a430294e7a3509c53ad2b
ap-runtime-63c91c8ef27841bca0b841f77c4cd9ac
ap-runtime-6a2c0402beae47eb877ba0696b5bd1ac
ap-runtime-73f296bae8dc4e0db20fa61f302b5a12
ap-runtime-7de6b469d1734edcb41d2cce864357cd
ap-runtime-80be882ff28348a1a693872768f80ff2
ap-runtime-95ad7ee35c7243a2a9e48364af26ad90
ap-runtime-97539af9e6b5432b86542b7df8fe6120
ap-runtime-a27523eb89b74d92b70b9b52a958bc05
ap-runtime-b1ac680bd8864deabd23d94c5d1fb4af
ap-runtime-b24f02a81b3d4c2fb8d554eecd8ed35d
ap-runtime-b5c96fed0f8744fca30b838dd4eec005
ap-runtime-b638ce3a3387461186631d0e78b03f39
ap-runtime-c447cbdd8ad147df987184ad7018b636
ap-runtime-db76108ff64647d6865182fec7d17e07
ap-runtime-f1fbfb0b3d7f4fda9da7c96f2f1cde96
ap-runtime-f3d765934cbf40f8b3912eb225c31cf6
ap-runtime-fa3d5acae1134ffa84d487e9782ec920
c4-admission-host-836f94b151d249fc9b76d6e43905c0bc
c4-ap-frame-host-4bd8396297434daba8b6d2d69f597667
c4-authority-host-4db516b353a041d4a7eeb89ef877a79c
c4-batch-backend-9adcb679823e46b994f71e550d2b4aec
c4-batch-core-c7a123fe8527472288056307994beefa
c4-batch-verified-host-3bdb7215e15248ce974eb1c775178ada
c5-bsp-final-4c19bd9a538e4f868e8de5291fe40027
c5-debug-gop-f80da89dbdd74306989d54fec4a2a7fd
c5-static-39821c74c84f43fc80918a895e9812c1
console-requests-362e603cd7254baeae912258e60c15ab
ipi-service-0c3e857b4a2441ef89fc656afdcdad0f
ipi-service-0d4c5bc8b0b644d7b0edc6488df124df
ipi-service-11bcfb42c84d4d368914df564c58d61b
ipi-service-2ed1d70dd4f4443488b416ef92b18741
ipi-service-4d4c587b14774a9cb3e378e2b76da963
ipi-service-5d5e1cbcebca4c39a0185a2e8e08d301
ipi-service-600a4b71228445ad8746fd67b7ee6b09
ipi-service-64e60a96d30b4a39a9b4239d5fd3be03
ipi-service-77a610aad5fa457fb8e05d15cb2855e9
ipi-service-845983f412bf4a83870f9ebced4cc5c0
ipi-service-88c243df16524dbd9afc3fff5ce9eddc
ipi-service-a15fab0ead3f42809b98909a70b0630a
ipi-service-b30f696adf504d0aa3c76f04be48c7a6
ipi-service-bba2c695889c42b3a304ba1e9375e4d0
ipi-service-d1fc8b61436c4043bd2fbe91dc0db98d
ipi-service-d495d1c672fd4c578967a2e79d4147be
ipi-service-ec99d86c7d954211a0e8deb960f37c72
ipi-service-host-3b196097b90d4bac89fa87258874ee13
ipi-service-host-a2653183328440caba011b5e311bc91f
ipi-service-host-acf65b5a0f93431b8cdd794f99973cb8
ipi-service-host-ba917eb9adf044b6a1be1ec9d0a01682
paging-owned-0713728fe2f648d9873b8a63eaff7a29
paging-tamper-ca0360d8c0e6486ba294623fd19ac8ac
physical-host-75e6a7102c5a4d52a6e6d1ca11698f61
pointer-interactive-check-5f525616539142caadb9615c23ef1dcf
pointer-interactive-check-88f33e0a3e36418aa8fe0a46e7ddd0a1
pointer-interactive-check-c1784e0240b54d74853c43d1bca9a47f
ps2-decoder-4b34b7b6ca16426c9a96e539baf493d6
qemu-run-0d9e74ffb3284db28f5827323e8c4c21
qemu-run-1d5020a2d2614bf79d83ab45a4c0a2ad
qemu-run-2ad0584d9e0b47b0acf99088e10e1a16
qemu-run-5627ac9414e244cea257e6be1693a722
qemu-run-5ca66c03d13242caba7bdc6a0c43e767
qemu-run-5f22cc06837a470a99cd7eb2847b5278
qemu-run-74324d7695064b16ad9aec9af892c9f7
qemu-run-80cc41c6fa8a44b3b4ff5c402da4faea
qemu-run-814e4ba23bf347a7b5ae150eb3279688
qemu-run-843e13ebbfab4704904c08c30d7375d7
qemu-run-953fb45aba6e4adaa57d1de48a257abd
qemu-run-95dfc8d5101b4b44927fd72769f5f0b5
qemu-run-b24fd16ce9c64a01b0b19f931a00786e
qemu-run-bd6855c67f074aeea3c4ae336acad779
qemu-run-be491cb6ce704db49ba387426cad6fe5
qemu-run-c9f04c98cb5348ddb280e7d9ea11537e
qemu-run-d1d98950fe424e51a6e9632041cb9ac7
qemu-run-da63bbddb1f944a09921eeb61d9c0e2e
qemu-run-debf8b7543a44943ad61d21278e355a2
qemu-run-ea96d156073d4e73b4cd37b15394b32e
scheduler-context-86bb8e0f61cf45dcb61d7320cd13f483
scheduler-context-d2e688e03dc748c39e84afd7f74be453
scheduler-context-eb2df731dc4b44b9a0ef6281dd65fe2c
scheduler-firmware-5172deff3c8846d7919e8c4646195b81
scheduler-host-0e66afd1ed8c4cfbadbf07c4a0550ecb
scheduler-host-3a9108bdf94f4fd29019361108f239b6
scheduler-host-5e4f7efac60d4c9bb57e128cb355ff4e
scheduler-host-7fc09a6612e94a3a888f0b2381eaa54c
scheduler-host-956a9e6d8e0c4127a7f0c480ae392ebd
scheduler-host-c5875f178d8146a48861d4e7472c5595
scheduler-host-cb65441ff0534d18a8f9fe72f3f6557f
scheduler-host-d689ab3f014a47f5ba9a12fcea241376
scheduler-host-f24376a62b6d4964ae2e4b2cfb2d27a9
test-run-07add93eaad9493e8a8304e4c475ab52
test-run-14998121be2f4fc8962dce896f6728bf
test-run-2a5a8de940fa4e5ca83bc06c8c1e9674
test-run-4602125d84fe46a7a581bc0e119dc629
test-run-72e3079edcf849b49d3920348d93b48d
test-run-82d4354b12104bffb1cddd0d70c1c32c
test-run-86c6d63b7ddb419d9707ebe7efaf81df
test-run-8b2c2ada1f49467388ef41a8a7c96755
test-run-96ddc10faf1e4171a65cea5af1955067
test-run-a446762f4cfb43078cd369857ffdcbc1
test-run-aa452f867292444091ceabdddafcc8c0
test-run-bed4189df7c74c65b337a601bc53cca9
test-run-c76ae287e2f6453dab0a9577a3de9d5a
test-run-de2897c0d3b14ed39cd00de4497f275b
watchdog-host-df46dddeb6ee4528989aecc7de79ad9c
```

</details>
