"""Bounded live-input soak; hardware trace confirms each half of every movement pair."""
import time


# ====== run ======

def run(vm, delivered, seconds):
    if seconds != 300:
        raise ValueError("The K3 soak is defined as 300 seconds")
    started = time.monotonic()
    pairs = 0
    first = delivered()
    while time.monotonic() - started < seconds:
        base = delivered()
        if base != first or not vm.alive() or vm.log().failures():
            raise RuntimeError("Soak input drift, VM exit or kernel failure")
        for movement, expected in ((1, (base[0] + 1, base[1])), (-1, base)):
            vm.command(f"mouse_move {movement} 0")
            deadline = time.monotonic() + 2
            while delivered() != expected:
                if time.monotonic() >= deadline or not vm.alive() or vm.log().failures():
                    raise RuntimeError("Soak mouse packet made no progress")
                time.sleep(0.005)
        pairs += 1
        if pairs % 40 == 0:
            print(f"SOAK: {time.monotonic() - started:.1f}s; {pairs * 2} hardware movement packets", flush=True)
        time.sleep(0.5)
    time.sleep(0.1)
    if delivered() != first:
        raise RuntimeError("Soak left net pointer movement")
    elapsed = time.monotonic() - started
    if elapsed < seconds or not pairs:
        raise RuntimeError("Soak duration or workload incomplete")
    return dict(Seconds=elapsed, RequestedSeconds=seconds, MovementPairs=pairs,
                HardwarePackets=pairs * 2, NetMovement=[0, 0])
