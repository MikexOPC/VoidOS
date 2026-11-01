"""Injected readback failure: no INIT to the next AP and retained resources."""
import argparse
import importlib.util
import time
import uuid
from pathlib import Path
spec = importlib.util.spec_from_file_location("sequence_parent", Path(__file__).with_name("firmware.py"))
parent = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parent)
cpu, qemu, transition = parent.cpu, parent.qemu, parent.transition

# ====== boot ======

def boot(vm):
    expected = ["KERNEL_AP_SEQUENCE_FAILED", "AP_IMAGE_INSTALLATION_FAILED", "KERNEL_SMP_PANIC"]
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        log = vm.log()
        if not vm.alive() or any(item not in expected for item in log.failures()):
            raise RuntimeError("Unexpected rearm failure outcome")
        if "KERNEL_SMP_PANIC_HALTED" in log.text:
            text = parent.registers(vm, 0)
            if "HLT=1" in text and parent.value(text, "RAX") == 0x50414e4943444f4e:
                break
        time.sleep(0.005)
    else:
        raise TimeoutError("Rearm failure did not reach coordinated panic")
    log = vm.log()
    vm.command("stop")
    if log.failures() != expected or log.number("KERNEL_AP_SEQUENCE_ERROR") != 10 or log.number("KERNEL_AP_SEQUENCE_STATE") != 1041:
        raise RuntimeError("Wrong injected error/token or extra failure")
    if log.has("KERNEL_AP_GROUP_PARKED") or log.has("KERNEL_AP_SEQUENCE_PARKED") or log.has("KERNEL_READY"):
        raise RuntimeError("Sequence continued after irreversible failure")
    root = log.number("KERNEL_AP_TRANSITION_ROOT")
    low = transition.low.verify(vm, log, False, expected)
    arena = cpu.ap_monitor.verify(vm, log, cpu.memory, 4, True, 1)
    state = cpu.memory(vm, log.number("KERNEL_PANIC_STATE"), 80)
    if state[:11] != [2, 15, 3, 1, 2, 2, 0, 2, 0, 2, 2] or any(state[11:]):
        raise RuntimeError("Wrong panic masks/ACK/records after failed rearm")
    if cpu.memory(vm, log.number("KERNEL_PANIC_OUTPUT"), 1) != [1] or cpu.memory(vm, log.number("KERNEL_PANIC_RECURSION"), 1) != [0]:
        raise RuntimeError("Incomplete or recursive emergency report")
    for name, value in (("TARGETS",2),("ACK",2),("MISSING",0),("OUTCOME",0)):
        if log.number("PANIC_"+name) != value:
            raise RuntimeError("Serial panic masks disagree")
    transition_owner = cpu.memory(vm, log.number("KERNEL_AP_TRANSITION_OWNER"), 19)
    pool = log.number("KERNEL_AP_LOW_POOL")
    if transition_owner[17:] != [4 | (1 << 32), 1] or cpu.memory(vm, pool, 3)[2] != transition_owner[13] - 4:
        raise RuntimeError("Transition freeze or budget lost")
    live, pins = cpu.memory(vm, pool + 4144, 256), cpu.memory(vm, pool + 6192, 32)
    data, tables = log.number("KERNEL_AP_TRANSITION_DATA"), log.number("KERNEL_AP_TRANSITION_TABLES")
    source = cpu.memory(vm, log.number("KERNEL_AP_TRANSITION_SOURCE"), 512)
    active = vm.physical(root, 512)
    if any(active[:256]) or any((a ^ b) & ~0x60 for a, b in zip(active, source)):
        raise RuntimeError("Operational root changed during failed rearm")
    code_address = int(low["Base"], 16)
    for table in range(4):
        expected_table = [0] * 512
        if table == 0:
            expected_table[0] = tables + 4096 | 3
            expected_table[256:] = source[256:]
            expected_table[510] = 0
        elif table < 3:
            expected_table[0] = tables + (table + 1) * 4096 | 3
        else:
            expected_table[code_address >> 12] = code_address | 1
            expected_table[data >> 12] = data | (1 << 63) | 3
        for actual, reference in zip(vm.physical(tables + table * 4096, 512), expected_table):
            mask = (0x60 if table == 3 else 0x20) if reference & 1 else 0
            if (actual ^ reference) & ~mask:
                raise RuntimeError("Transition tables changed during failed rearm")
    for physical, pages, ident in ((data, 1, transition_owner[5]), (tables, 4, transition_owner[9])):
        slots = [i for i in range(64) if live[i*4:i*4+4] == [physical,pages,ident,pool]]
        if len(slots) != 1 or (pins[slots[0]//2] >> (32*(slots[0]%2))) & 0xffffffff != 1:
            raise RuntimeError("Transition lease released or unpinned")
    code = int(low["Base"],16)
    if vm.physical(code,512) != cpu.memory(vm,log.number("KERNEL_AP_IMAGE_CODE_EXPECTED"),512):
        raise RuntimeError("Code changed during failed rearm")
    first = log.number("KERNEL_AP_CONTEXTS")
    base = log.number("KERNEL_AP_BASE")
    new_data = cpu.memory(vm, log.number("KERNEL_AP_IMAGE_DATA_EXPECTED"),512)
    new_data[1] = 2 | (2 << 32); new_data[2] = 1040
    new_data[7] = base + 2*0x40000 + 17*4096
    new_data[8] = base + 2*0x40000 + 48*4096
    new_data[9] = first + 448
    if vm.physical(data,512) != new_data:
        raise RuntimeError("Physical write was partial or differs from the next record")
    for index in range(1,4):
        text = parent.registers(vm,index)
        (vm.directory/f"cpu-{index}.txt").write_text(text)
        context = cpu.memory(vm,first+(index-1)*448,56)
        if context[2] != index | ((2 if index==1 else 1)<<32) or context[4] != 1 or any(context[6:]):
            raise RuntimeError("Wrong retained CPU state")
        if index==1:
            if "HLT=1" not in text or parent.value(text,"RAX") != 0x50414e494353544f or parent.value(text,"CR3") != root:
                raise RuntimeError("Previous AP did not acknowledge the panic")
        elif parent.value(text,"CR3") == root or cpu.gs_base(text) == first + (index-1)*448:
            raise RuntimeError("Next AP entered the OS context after failed rearm")
    return dict(Injected="readback-copy-corruption",OsCpusReady=2,NextApStarted=False,Token=1040,Retained=True,ApResources=arena)

# ====== main ======

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--image",type=Path,required=True)
    args=parser.parse_args()
    out=transition.ROOT/"kernel/build"/("sequence-failure-"+uuid.uuid4().hex)
    out.mkdir(); rows=[]
    try:
        with qemu.Vm(out/"boot-1",args.image,"std",smp=4) as vm:
            rows.append(boot(vm))
    except Exception as error:
        rows.append(dict(Failure=str(error)))
        qemu.finish(out,"sequence-failure",args.image,rows,0,1)
        raise
    if not qemu.finish(out,"sequence-failure",args.image,rows,1,1): raise SystemExit(1)

if __name__=="__main__": main()
