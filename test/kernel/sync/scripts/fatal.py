"""Fail-stop is observable in a separate process; an issued ticket must not be cancelled."""
import subprocess
import sys


# ====== main ======

def main():
    cases = {
        "deadline": ("LOCK_WAIT_FAILED", "next=2 serving=0"),
        "stopped": ("LOCK_WAIT_FAILED", "next=2 serving=0"),
        "failed": ("LOCK_WAIT_FAILED", "next=2 serving=0"),
        "no-clock": ("LOCK_WAIT_FAILED", "next=2 serving=0"),
        "clock-if": ("LOCK_CLOCK_IRQ_ENABLED", "next=2 serving=0"),
        "release-if": ("LOCK_RELEASE_IRQ_ENABLED", "next=1 serving=0"),
        "nested-if": ("LOCK_NESTED_IRQ_ENABLED", "next=1 serving=0"),
    }
    for name, (reason, queue) in cases.items():
        result = subprocess.run([sys.argv[1], "fatal", name], capture_output=True, text=True, timeout=10)
        if result.returncode != 86 or reason not in result.stdout or queue not in result.stdout:
            raise RuntimeError(f"Unexpected fail-stop {name}: {result.returncode}; {result.stdout}; {result.stderr}")
        print("PASS: expected fail-stop " + name + "; ticket retained", flush=True)


if __name__ == "__main__":
    main()
