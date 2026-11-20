import argparse
import importlib.util
from pathlib import Path
import uuid

ROOT = Path(__file__).resolve().parents[5]
spec = importlib.util.spec_from_file_location('service', ROOT / 'test/arch/x86/kernel/cpu/percpu/service/integration/firmware.py')
service = importlib.util.module_from_spec(spec)
spec.loader.exec_module(service)

# ====== verify_channel ======

def verify_channel(vm, log, negative, notified=False):
    address = log.number('KERNEL_AP_CONSOLE_RECORD')
    record = service.cpu.memory(vm, address, 15)
    root = service.launch.value(service.launch.registers(vm, 0), 'CR3')
    pool, live, pins, free = service.runtime.allocator(vm, log)
    expected = [address, root, root, 1, 1, 1000, 1000, 1000, 1000,
                1 if negative and not notified else 3, 0 if negative and not notified else 1, int(negative)]
    if record[:12] != expected or record[12] < 32 or record[13] != record[12] - 32:
        raise RuntimeError('Channel identity, FULL, FIFO counts or lifecycle differs: ' + str(record))
    batch = service.cpu.memory(vm, log.number('KERNEL_AP_BATCH_RECORD'), 32)
    if not negative:
        if record[14] != record[12] or free != record[14] or batch[3:6] != record[12:15]:
            raise RuntimeError('Physical budget not restored')
    else:
        if (record[14] or free != record[13] or batch[:3] != [log.number('KERNEL_AP_BATCH_RECORD'), root, pool] or
                batch[3:6] != [record[12], record[13], 0] or batch[20] != 2 or batch[26:28] != [0, 0] or
                not log.before('KERNEL_AP_NOTIFY_INCOMPLETE' if notified else 'KERNEL_AP_CONSOLE_INCOMPLETE', 'KERNEL_SMP_PANIC')):
            raise RuntimeError('Missing completion did not deny cleanup')
        leaves = service.cpu.memory(vm, log.number('KERNEL_AP_BATCH_PTE'), 36)
        frames = []
        for task in range(2):
            entries = leaves[18 * task:18 * (task + 1)]
            frame = entries[1] & 0x000ffffffffff000
            if not frame or entries[0] or entries[17] or any(
                    (entries[i + 1] & ~0x60) != (frame + 4096 * i | 3 | (1 << 63)) for i in range(16)):
                raise RuntimeError('Retained stack or guards invalid')
            matches = [i for i in range(len(live) // 4) if live[4 * i] == frame and live[4 * i + 2]]
            if len(matches) != 1 or live[4 * matches[0] + 1] != 16 or (
                    pins[matches[0] // 2] >> (32 * (matches[0] % 2))) & 0xffffffff != 1:
                raise RuntimeError('Retained stack lease missing')
            frames.append(frame)
        if abs(frames[0] - frames[1]) < 65536:
            raise RuntimeError('Retained stacks overlap')
    return dict(ProducerCpu=1, ConsumerCpu=0, Writes=2000, ForcedFull=True,
                MissingCompletion=negative and not notified, NotificationFailure=negative and notified, BudgetBefore=record[12], BudgetHeld=record[13], BudgetFinal=record[14])

# ====== verify_notification ======

def verify_notification(vm, log, case):
    address = log.number('KERNEL_AP_NOTIFY_RECORD')
    record = service.cpu.memory(vm, address, 16)
    root = service.launch.value(service.launch.registers(vm, 0), 'CR3')
    negative = case != 'positive'
    if (record[:2] != [address, root] or not record[2] or
            record[3] != int(negative) or not 1 <= record[4] <= 2000 or
            not 1 <= record[5] <= record[4] or record[6] != record[5] or not record[7] or
            record[8:10] != [int(not negative), int(negative)]):
        raise RuntimeError('IPI identity, send/delivery/EOI or close differs: ' + str(record))
    state, published, observed, pending, slot0, slot1 = record[10:]
    if (state != (10 if case == 'inflight' else 2) or published != 2000 or
            observed != (0 if case == 'no-ack' else 2000) or pending or slot0 or
            (case == 'inflight' and not 2 <= slot1 <= 4001) or
            (case != 'inflight' and slot1)):
        raise RuntimeError('Software notification sequence/reference accounting differs: ' + str(record))
    return dict(Vector=0x43, Event=7, Published=published, Observed=observed,
                IcrSends=record[4], Delivered=record[5], Eois=record[6],
                Drains=record[7], Closed=bool(record[8]), Denied=bool(record[9]))

# ====== verify_worker ======

def verify_worker(vm, log, case):
    address = log.number('KERNEL_AP_WORKER_RECORD')
    record = service.cpu.memory(vm, address, 14)
    (self, generation, waits, woken, changed, idles, slept,
     ipi_wakes, ipi_halts, drained, steps, exited, released, closed) = record
    if (self != address or not generation or not waits or waits != woken + changed or not woken or
            not idles or slept < 2 * idles or not ipi_halts or ipi_halts > ipi_wakes or ipi_wakes > idles or
            steps < 2 or [exited, released] != [1, 1] or closed != int(case == 'positive')):
        raise RuntimeError('BSP worker WAIT/HLT wake or cleanup differs: ' + str(record))
    return dict(Waits=waits, Woken=woken, Changed=changed, Idles=idles, HltWakes=slept - idles,
                IpiWakes=ipi_wakes, IpiHalts=ipi_halts, Drained=drained, Steps=steps)

# ====== main ======

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--image', type=Path, required=True)
    parser.add_argument('--boots', type=int, choices=range(1, 11), default=10)
    parser.add_argument('--incomplete', action='store_true')
    parser.add_argument('--ipi', choices=('positive','no-ack','inflight'))
    args = parser.parse_args()
    if args.incomplete and args.ipi: parser.error("Choose one negative profile")
    negative = args.incomplete or args.ipi in ("no-ack","inflight")
    out = ROOT / 'kernel/build' / ('ap-console-' + uuid.uuid4().hex)
    out.mkdir()
    results = []
    passed = 0
    try:
        for iteration in range(1, args.boots + 1):
            with service.qemu.Vm(out / f'boot-{iteration}', args.image, 'std', smp=4) as vm:
                try:
                    log = service.await_terminal(vm, 'ap-console-incomplete' if negative else 'ap-batch')
                    vm.command('stop')
                    if negative:
                        group_spec = importlib.util.spec_from_file_location('group',
                            ROOT / 'test/arch/x86/kernel/cpu/percpu/panic/integration/group.py')
                        group = importlib.util.module_from_spec(group_spec)
                        group_spec.loader.exec_module(group)
                        group.settle(vm, log.number('PANIC_OWNER'), 4)
                        log = vm.log()
                    else:
                        if not log.has('KERNEL_AP_CONSOLE_READY'):
                            raise RuntimeError('Missing AP console completion')
                    proof = service.verify(vm, log, 'ap-console-incomplete' if negative else 'ap-batch',
                                           4, batch_steps=65536, extra_shootdowns=1 if args.ipi else 0)
                    counters = service.cpu.memory(vm, log.number('KERNEL_IPI_CPUS') if log.has_number('KERNEL_IPI_CPUS') else log.number('KERNEL_IPI_NATIVE') + 4160, 64 * 8)
                    base = log.number('KERNEL_IPI_BASE_GENERATION')
                    if any(counters[i * 8] != counters[i * 8 + 4] or
                           counters[i * 8] != counters[i * 8 + 5] or counters[i * 8] < base for i in range(1, 4)):
                        raise RuntimeError('IPI delivery/EOI accounting differs')
                    proof.update(Case=('ap-console-ipi-' + args.ipi) if args.ipi else ('ap-console-incomplete' if negative else 'ap-console'),
                                 TaskCpus=[1], AdmittedCpus=[1],
                                 Requests=sum(counters[i * 8] - base for i in range(1, 4)))
                    result = dict(**proof, Channel=verify_channel(vm, log, negative, bool(args.ipi)))
                    if args.ipi:
                        result["Notification"] = verify_notification(vm, log, args.ipi)
                        result["Worker"] = verify_worker(vm, log, args.ipi)
                    results.append(result)
                except BaseException:
                    service.capture_failure(vm, 4)
                    raise
            passed += 1
            print(f'PASS: AP console {iteration}/{args.boots}; FIFO, FULL, quiescence and physical ownership', flush=True)
    except Exception as error:
        results.append(dict(Failure=str(error)))
        service.qemu.finish(out, 'ap-console', args.image, results, passed, args.boots)
        raise
    if not service.qemu.finish(out, 'ap-console', args.image, results, passed, args.boots):
        raise SystemExit(1)

if __name__ == '__main__':
    main()
