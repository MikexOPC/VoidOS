"""Independent VT-d activation witness: guest report against the QEMU v11.1.0 intel_iommu trace."""
import re

QIE,SRTP,TE=1<<26,1<<30,1<<31
READY,TRANSLATING,UNPUBLISHED=QIE,TE|SRTP|QIE,SRTP|QIE  # GSTS TES31/RTPS30/QIES26 share the GCMD bit positions

# ====== numbers ======

def numbers(log,name):
    return [int(value,16) for value in re.findall(re.escape(name)+r': 0x([0-9a-fA-F]{16})',log)]

def one(log,name):
    values=numbers(log,name)
    if len(values)!=1: raise RuntimeError('Activation evidence count: '+name)
    return values[0]

# ====== verify ======

# Intel VT-d5.0 11.4.2 CAP FRO33:24/NFR47:40/RWBF4, 11.4.4.2 GSTS, 11.4.5 RTADDR, 11.4.7.1 FSTS PFO0/PPF1.
def verify(log,routing):
    if 'KERNEL_ACTIVATION_OK' not in log or 'KERNEL_ACTIVATION_RETAINED' in log or 'KERNEL_ACTIVATION_FAILURE' in log:
        raise RuntimeError('Activation did not complete')
    if one(log,'KERNEL_ACTIVATION_BUDGET_BEFORE')!=one(log,'KERNEL_ACTIVATION_BUDGET_AFTER'): raise RuntimeError('Activation budget')
    cap,root,source=one(log,'KERNEL_ACTIVATION_CAP'),one(log,'KERNEL_ACTIVATION_ROOT'),one(log,'KERNEL_ACTIVATION_SOURCE')
    if cap&16 or one(log,'KERNEL_ACTIVATION_FRO')!=((cap>>24)&1023)*16 or one(log,'KERNEL_ACTIVATION_NFR')!=((cap>>40)&255)+1:
        raise RuntimeError('Fault geometry differs from CAP')
    if not root or root&4095 or root>=1<<52: raise RuntimeError('Root table address')
    target=(one(log,'KERNEL_ECAM_TARGET_BUS')<<8)|(one(log,'KERNEL_ECAM_TARGET_DEVICE')<<3)|one(log,'KERNEL_ECAM_TARGET_FUNCTION')
    if source!=target or routing.get('Unit')!=0: raise RuntimeError('Activation source differs from the routed GPU')
    phases=re.findall(r'KERNEL_ACTIVATION_PHASE: (\w+)',log)
    if phases!=['READY','TRANSLATING','UNPUBLISHED']: raise RuntimeError('Activation phase order')
    gsts,rtaddr,fsts=numbers(log,'KERNEL_ACTIVATION_GSTS'),numbers(log,'KERNEL_ACTIVATION_RTADDR'),numbers(log,'KERNEL_ACTIVATION_FSTS')
    if gsts!=[READY,TRANSLATING,UNPUBLISHED] or rtaddr!=[0,root,0] or any(value&3 for value in fsts) or len(fsts)!=3:
        raise RuntimeError('Activation register witness')
    owned,blocked=one(log,'KERNEL_ACTIVATION_OWNED'),one(log,'KERNEL_ACTIVATION_BLOCKED')
    sources=numbers(log,'KERNEL_ACTIVATION_FAULT_SOURCE')
    if owned or source in sources or len(sources)!=min(blocked,16) or one(log,'KERNEL_ACTIVATION_LOGGED')!=len(sources):
        raise RuntimeError('Owned requester faulted or fault log inconsistent')
    return dict(Root=root,Source=source,Blocked=blocked,Overflows=one(log,'KERNEL_ACTIVATION_OVERFLOWS'),
        TranslationObserved=True,TornDown=True,BudgetRestored=True,OwnedFaults=0)

# ====== verify_trace ======

def verify_trace(directory,log):
    lines=(directory/'routing.trace').read_text(errors='replace').splitlines()
    root,source=one(log,'KERNEL_ACTIVATION_ROOT'),one(log,'KERNEL_ACTIVATION_SOURCE')
    fro,nfr=one(log,'KERNEL_ACTIVATION_FRO'),one(log,'KERNEL_ACTIVATION_NFR')
    events=[]
    for line in lines:
        match=re.fullmatch(r'vtd_reg_write addr (0x[0-9a-f]+) size (0x[0-9a-f]+) value (0x[0-9a-f]+)',line)
        if match: events.append(('write',)+tuple(int(value,16) for value in match.groups())); continue
        name=line.split(' ',1)[0]
        events.append((name,line))
    writes=[event[1:] for event in events if event[0]=='write']
    for offset,width,value in writes:
        frcd=fro<=offset<fro+16*nfr and (offset-fro)%16==8
        if not ((offset==0x18 and width==4 and value in (0,QIE,QIE|SRTP,QIE|TE)) or (offset==0x20 and width==8 and value in (0,root)) or
                (offset in (0x88,0x90) and width==8) or (offset==0x34 and width==4 and value==1) or (frcd and width==8 and value==1<<63)):
            raise RuntimeError(f'Register write outside the whitelist: {offset:#x}/{width}={value:#x}')
    queue=next((value for offset,_,value in writes if offset==0x90 and value),0)
    control=[(offset,value) for offset,_,value in writes if offset in (0x18,0x20) or (offset in (0x88,0x90) and value in (0,queue))]
    expected=[(0x88,0),(0x90,queue),(0x18,QIE),(0x20,root),(0x18,QIE|SRTP),(0x18,QIE|TE),(0x18,QIE),(0x20,0),(0x18,0),(0x88,0),(0x90,0)]
    if not queue or queue&4095 or control!=expected: raise RuntimeError('Activation command sequence')
    order=[event[0] if event[0]!='write' else 'write' for event in events]
    position=lambda name,start=0: order.index(name,start)
    roots=[event[1] for event in events if event[0]=='vtd_reg_dmar_root']
    enables=[event[1] for event in events if event[0]=='vtd_dmar_enable']
    if roots!=[f'vtd_reg_dmar_root addr {root:#x} scalable 0'] or enables!=['vtd_dmar_enable enable 1','vtd_dmar_enable enable 0']:
        raise RuntimeError('Root publication or enable trace')
    published,on,off=position('vtd_reg_dmar_root'),position('vtd_dmar_enable'),position('vtd_dmar_enable',position('vtd_dmar_enable')+1)
    triplet=['vtd_inv_desc_cc_global','vtd_inv_desc_iotlb_global','vtd_inv_desc_wait_sw']
    descriptors=[(index,name) for index,name in enumerate(order) if name in triplet]
    if [name for _,name in descriptors]!=triplet*2 or not published<descriptors[0][0]<descriptors[2][0]<on<off<descriptors[3][0]:
        raise RuntimeError('Invalidation must follow SRTP and TE=0')
    faults=sum(1 for name in order if name=='vtd_dmar_fault'); recorded=sum(1 for name in order if name=='vtd_frr_new')
    bdf=f'{source>>8:02x}:{(source>>3)&31:02x}.{source&7}'
    switched=[event[1] for event in events[on:off] if event[0]=='vtd_switch_address_space']
    if f'vtd_switch_address_space Device {bdf} switching address space (iommu enabled=1)' not in switched: raise RuntimeError('Requester not translated')
    restored=[event[1] for event in events[off:] if event[0]=='vtd_switch_address_space']
    if f'vtd_switch_address_space Device {bdf} switching address space (iommu enabled=0)' not in restored: raise RuntimeError('Requester not restored')
    if recorded!=one(log,'KERNEL_ACTIVATION_BLOCKED') or faults<recorded: raise RuntimeError('Recorded faults differ from the guest report')
    return dict(Writes=len(writes),Whitelist=True,SequenceExact=True,InvalidationsAfterSrtpAndTe0=2,
        TranslatedDevices=sum(1 for line in switched if line.endswith('enabled=1)')),QemuFaults=faults,QemuRecords=recorded)
