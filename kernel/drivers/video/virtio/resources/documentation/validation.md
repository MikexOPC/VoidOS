# Risorse VirtIO GPU — validazione

I report completi precedenti sono in `kernel/drivers/video/virtio/resources/documentation/` al commit `49f8cef`.
I risultati riportati riguardano le immagini dei rispettivi checkpoint.

La validazione scanout/aggiornamento parziale passa 10/10,
assenza GPU 10/10, risorse senza scanout 10/10 e regressione
console/input 10/10. Mancata conferma e feature non supportata
sono rifiutate in un boot per caso. Le prove distinte di
mappatura e code non dimostrano da sole una sessione GPU attiva.
I risultati VT-d della sessione sono nella validazione del
consolidamento.

[Design del modulo](design.md).

<details>
<summary>Directory delle prove storiche</summary>

```text
virtio-initialization-firmware-30d38cfcc4444b5f8cb3e331b418d8c8
virtio-initialization-firmware-39f620f2127142fa861f37b205c48405
virtio-initialization-firmware-3e63b6d14c754390bf6a34a3339ce58f
virtio-initialization-firmware-5da470c254124e949a9f3f2789c8ad01
virtio-initialization-firmware-764f8c12dbc24eec8dffcc7201e2c1ea
virtio-initialization-firmware-830c1664e75d46968ed1962231007507
virtio-initialization-firmware-8e51f251ebe34ad1b990c75087e6e0b0
virtio-initialization-firmware-ade12a0ea1f64e49a74e91072be15e92
virtio-initialization-firmware-d10c9b7cafae45be8528a7cfd8841b6b
virtio-initialization-firmware-dce10d0ba66c4642b1db42d6832580d4
virtio-initialization-firmware-f5b94f983353423394b536abad038f34
virtio-mapping-firmware-079c706c904e439abe1d1aed4f9b3033
virtio-mapping-firmware-58f59bba7ae5446b95d3205d0ada8463
```

</details>
