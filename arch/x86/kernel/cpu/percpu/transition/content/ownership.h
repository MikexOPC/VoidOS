#ifndef OS_X86_AP_CONTENT_OWNERSHIP_H
#define OS_X86_AP_CONTENT_OWNERSHIP_H
#include "../transition.h"
int xt_content_busy(const X86_AP_TRANSITION *transition);
int xt_content_held(const X86_AP_TRANSITION *transition, const void *borrower);
int xt_content_hold(X86_AP_TRANSITION *transition, const void *borrower);
int xt_content_unhold(X86_AP_TRANSITION *transition, const void *borrower);
#endif
