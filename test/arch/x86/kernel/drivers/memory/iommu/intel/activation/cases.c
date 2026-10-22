#include "model.h"
static X86_VTD_DOMAIN domain;
static X86_VTD_ACTIVATION act;
static X86_VTD_ROUTE route;
static X86_VTD_DOMAIN_REQUEST request;
static X86_PM_CLOCK clock={0,0,0x408,0xffffff,1};
#define QIE X86_QI_ENABLE
#define TES X86_VTD_GSTS_TES
#define RTPS X86_VTD_GSTS_RTPS

// ====== open_private ======

static void open_private(void)
{
    act_fixture();
    CHECK(x86_qi_prepare(&mapping,&clock,52,&qi));
    memset(&request,0,sizeof(request)); memset(&domain,0,sizeof(domain)); memset(&act,0,sizeof(act));
    request.Device=1; request.Count=1; request.PhysicalBits=52; request.Buffers[0].Access=3;
    CHECK(kernel_physical_allocate(&pool,1,&request.Buffers[0].Lease) && kernel_physical_pin(&pool,&request.Buffers[0].Lease,1));
    CHECK(x86_vtd_domain_prepare(&context,&window,0,&inventory,&request,&domain));
    CHECK(x86_qi_start(&qi));
    memset(&route,0,sizeof(route)); route.Base=mapping.Authority.Base[0]; route.Bytes=4096; route.Kind=1; route.Requester=0x08;
}

// ====== close_private ======

static void close_private(void)
{
    CHECK(x86_vtd_domain_release(&domain));
    CHECK(kernel_physical_pin(&pool,&request.Buffers[0].Lease,0) && kernel_physical_release(&pool,&request.Buffers[0].Lease));
    qi_close();
}

// ====== abandon_private ======

// Host-only: after a RETAINED activation the model is forced idle so the fixture can be reused.
static void abandon_private(void)
{
    CHECK(act.State==X86_VTD_ACT_RETAINED && act.Self && !x86_vtd_activation_end(&act) &&
        !x86_vtd_activation_disable(&act) && !x86_vtd_activation_faults(&act) && domain.Self && qi.Self);
    act_set_status(QIE); rtaddr=0; fsts=0; memset(&act,0,sizeof(act));
    CHECK(x86_qi_status_allow(&qi,0));
    close_private();
}

// ====== log_private ======

static void log_private(BOOT_U32 index,BOOT_U32 offset,BOOT_U64 value)
{ CHECK(index<act_writes && act_log[index].Offset==offset && act_log[index].Value==value); }

// ====== happy_private ======

static void happy_private(unsigned cycle)
{
    open_private();
    unsigned before=completions; BOOT_U32 next=0;
    CHECK(x86_vtd_activation_begin(&qi,&domain,&route,&act) && act.State==X86_VTD_ACT_READY && act.Source==8 &&
        act.FaultOffset==ACT_FRO && act.FaultCount==ACT_NFR && act.Root==domain.Tables[0].Base);
    CHECK(x86_vtd_activation_faults(&act) && act.Scans==1 && !act_writes);
    CHECK(x86_vtd_activation_enable(&act) && act.State==X86_VTD_ACT_TRANSLATING && rtaddr==act.Root);
    CHECK(act_status()==(TES|RTPS|QIE) && completions==before+1 && qi.Status==(TES|RTPS));
    log_private(next++,0x20,act.Root); log_private(next++,0x18,QIE|X86_VTD_GCMD_SRTP); log_private(next++,0x18,QIE|X86_VTD_GCMD_TE);
    CHECK(!x86_vtd_activation_enable(&act) && !x86_vtd_activation_end(&act));
    if (cycle&1) {
        act_inject(1,0xfa,2,cycle&2?1:0,0x5123);
        CHECK(x86_vtd_activation_faults(&act) && act.Blocked==1 && act.Logged==1 && !(fsts&3));
        CHECK(act.Log[0].Source==0xfa && act.Log[0].Index==1 && act.Log[0].Address==0x5000 && act.Log[0].Reason==2 &&
            act.Log[0].Read==((cycle&2)?1U:0U));
        log_private(next++,ACT_FRO+16+8,1ULL<<63);
    }
    CHECK(x86_vtd_activation_disable(&act) && act.State==X86_VTD_ACT_UNPUBLISHED && !rtaddr);
    CHECK(act_status()==(RTPS|QIE) && completions==before+2 && qi.Status==RTPS);
    log_private(next++,0x18,QIE); log_private(next++,0x20,0); CHECK(act_writes==next && !refused);
    CHECK(!x86_vtd_activation_disable(&act) && x86_vtd_activation_faults(&act));
    CHECK(x86_vtd_activation_end(&act) && !act.Self);
    CHECK(x86_qi_submit(&qi) && x86_qi_finish(&qi)); // QI keeps working beside the sticky RTPS bit.
    close_private();
}

// ====== fault_cases ======

static void fault_cases(void)
{
    open_private();
    CHECK(x86_vtd_activation_begin(&qi,&domain,&route,&act) && x86_vtd_activation_enable(&act));
    act_inject(0,0x08,6,1,0x1001000);
    CHECK(!x86_vtd_activation_faults(&act) && act.Owned==1 && act.Log[0].Reason==6 && act.Log[0].Read==1 && !(fsts&3));
    CHECK(x86_vtd_activation_disable(&act) && !x86_vtd_activation_end(&act) && act.Self);
    act.Owned=0; CHECK(x86_vtd_activation_end(&act)); close_private();

    open_private();
    CHECK(x86_vtd_activation_begin(&qi,&domain,&route,&act) && x86_vtd_activation_enable(&act));
    act_inject(2,0xf8,2,0,0x2000); act_inject(2,0xf8,2,0,0x3000); act_inject(3,0xfa,2,0,0x4000);
    CHECK(fsts==(3U|(2U<<8)));
    CHECK(x86_vtd_activation_faults(&act) && act.Overflows==1 && act.Blocked==2 && !(fsts&3) && act.Log[1].Index==3);
    for (unsigned round=0; round<5; ++round) {
        refill=1; act_inject(1,0xfa,2,0,0x6000);
        CHECK(!x86_vtd_activation_faults(&act) && (fsts&2));
        refill=0; CHECK(x86_vtd_activation_faults(&act) && !(fsts&3));
    }
    CHECK(act.Blocked==2+25 && act.Logged==X86_VTD_FAULT_LOG && !act.Owned);
    act.Scans=~0U; CHECK(!x86_vtd_activation_faults(&act)); act.Scans=0;
    records[0][1]=(1ULL<<63)|(1ULL<<20); fsts=2; // reserved bit 84 set: unreadable record
    CHECK(!x86_vtd_activation_faults(&act)); records[0][1]=0; fsts=0;
    fsts=2|(7U<<8); CHECK(!x86_vtd_activation_faults(&act)); fsts=0; // FRI beyond NFR
    CHECK(x86_vtd_activation_disable(&act) && x86_vtd_activation_end(&act)); close_private();
}

// ====== refusal_cases ======

static void refusal_cases(void)
{
    open_private();
    X86_VTD_ROUTE good=route;
    route.Unit=1; CHECK(!x86_vtd_activation_begin(&qi,&domain,&route,&act)); route=good;
    route.Kind=0; CHECK(!x86_vtd_activation_begin(&qi,&domain,&route,&act)); route=good;
    route.Kind=4; CHECK(!x86_vtd_activation_begin(&qi,&domain,&route,&act)); route=good;
    route.Requester=9; CHECK(!x86_vtd_activation_begin(&qi,&domain,&route,&act)); route=good;
    route.Base+=0x10000; CHECK(!x86_vtd_activation_begin(&qi,&domain,&route,&act)); route=good;
    route.Segment=1; CHECK(!x86_vtd_activation_begin(&qi,&domain,&route,&act)); route=good;
    route.Reserved=1; CHECK(!x86_vtd_activation_begin(&qi,&domain,&route,&act)); route=good;
    qi.Cap|=16; CHECK(!x86_vtd_activation_begin(&qi,&domain,&route,&act)); qi.Cap&=~16ULL;
    domain.State=2; CHECK(!x86_vtd_activation_begin(&qi,&domain,&route,&act)); domain.State=3;
    CHECK(!x86_vtd_activation_begin(&qi,&domain,&route,(X86_VTD_ACTIVATION *)&qi));
    CHECK(!x86_vtd_activation_begin(&qi,&domain,(X86_VTD_ROUTE *)&act,&act));
    act.Logged=1; CHECK(!x86_vtd_activation_begin(&qi,&domain,&route,&act)); act.Logged=0;
    CHECK(x86_qi_submit(&qi)); CHECK(!x86_vtd_activation_begin(&qi,&domain,&route,&act)); CHECK(x86_qi_finish(&qi));
    CHECK(!x86_qi_status_allow(&qi,1U<<26) && !x86_qi_status_allow(&qi,1U<<29));
    act_set_status(QIE|RTPS); CHECK(!x86_qi_submit(&qi)); act_set_status(QIE); // QI rejects foreign GSTS bits.
    CHECK(!act.Self && !act_writes);
    CHECK(x86_vtd_activation_begin(&qi,&domain,&route,&act));
    act_set_status(QIE|RTPS); CHECK(!x86_vtd_activation_enable(&act)); act_set_status(QIE);
    rtaddr=0x5000; CHECK(!x86_vtd_activation_enable(&act)); rtaddr=0;
    fsts=2; CHECK(!x86_vtd_activation_enable(&act)); fsts=0;
    CHECK(x86_qi_status_allow(&qi,RTPS) && !x86_vtd_activation_enable(&act) && x86_qi_status_allow(&qi,0));
    CHECK(act.State==X86_VTD_ACT_READY && !act_writes && !x86_vtd_activation_disable(&act));
    CHECK(!x86_vtd_activation_write(&act,0x18,4,QIE|X86_VTD_GCMD_SRTP|X86_VTD_GCMD_TE) && !x86_vtd_activation_write(&act,0x20,8,0x9000));
    CHECK(!x86_vtd_activation_write(&act,0x90,8,0) && x86_vtd_activation_read(&act,0x88,8)==~0ULL && refused==4 && !act_writes);
    CHECK(x86_vtd_activation_end(&act) && !act.Self);
    close_private();
}

// ====== retention_cases ======

static void retention_cases(void)
{
    BOOT_U32 *stalls[]={&bad_readback,&stall_srtp,&stall_te_on};
    const BOOT_U32 failures[]={1,2,4};
    for (unsigned index=0; index<3; ++index) {
        open_private();
        CHECK(x86_vtd_activation_begin(&qi,&domain,&route,&act)); *stalls[index]=1;
        CHECK(!x86_vtd_activation_enable(&act) && act.Failure==failures[index]);
        if (index==2) CHECK(!x86_qi_stop(&qi) && qi.Self); // TES may still be set: the queue stays owned
        *stalls[index]=0; abandon_private();
    }
    open_private();
    CHECK(x86_vtd_activation_begin(&qi,&domain,&route,&act) && x86_vtd_activation_enable(&act));
    stall_te_off=1; CHECK(!x86_vtd_activation_disable(&act) && act.Failure==6 && (act_status()&TES));
    CHECK(!x86_qi_stop(&qi) && qi.Pins==3 && domain.Allocated==domain.TableCount);
    stall_te_off=0; abandon_private();
}

// ====== main ======

int main(void)
{
    for (unsigned cycle=0; cycle<64; ++cycle) happy_private(cycle);
    fault_cases(); refusal_cases(); retention_cases();
    printf("PASS %u VT-d activation checks; 64 SRTP/TE cycles, blocked/owned faults, overflow, refill, refusals and retention\n",checks);
    return 0;
}
