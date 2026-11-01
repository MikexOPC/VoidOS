; ====== embedded_trusted_template ======

section .rdata align=4096
global xs_blob_template
xs_blob_template:
    incbin "ap-template.bin"
