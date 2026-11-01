#ifndef OS_X86_AP_BLOB_INSTALL_H
#define OS_X86_AP_BLOB_INSTALL_H
#include "../blob/blob.h"
#include "../../transition/transition.h"

// ====== installation_owner ======

typedef struct {
    uint64_t  Self;
    X86_AP_TRANSITION  *Transition;
    const unsigned char  *Template;
    XS_ENTRY_RECORD  Record;
    uint32_t  State, Reserved;
    uint64_t  Code[512], Data[512], Scratch[512];
} XB_INSTALLATION;
_Static_assert(offsetof(XB_INSTALLATION, Code) == 136 && offsetof(XB_INSTALLATION, Data) == 4232 &&
    sizeof(XB_INSTALLATION) == 12424, "Installation monitor ABI");

int xb_install(XB_INSTALLATION *owner, X86_AP_TRANSITION *transition,
    const unsigned char *template_bytes, XS_ENTRY_RECORD record);
int xb_installed(XB_INSTALLATION *owner);
int xb_erase(XB_INSTALLATION *owner);
int xb_seal(XB_INSTALLATION *owner);
int xb_sealed(XB_INSTALLATION *owner);
#endif
