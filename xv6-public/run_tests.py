import subprocess
import time
import os
import pty
import select
import sys

def run_xv6_interactive():
    master, slave = pty.openpty()
    
    cmd = [
        "qemu-system-i386",
        "-nographic",
        "-drive", "file=fs.img,index=1,media=disk,format=raw",
        "-drive", "file=xv6.img,index=0,media=disk,format=raw",
        "-smp", "2",
        "-m", "512"
    ]
    
    proc = subprocess.Popen(
        cmd,
        cwd="/mnt/c/Users/avenu/Documents/OS_projecy/xv6-public",
        stdin=slave,
        stdout=slave,
        stderr=slave,
        close_fds=True
    )
    os.close(slave)
    
    def read_until(pattern, timeout=10):
        buffer = ""
        start = time.time()
        while time.time() - start < timeout:
            r, _, _ = select.select([master], [], [], 0.5)
            if r:
                chunk = os.read(master, 1024).decode('utf-8', errors='ignore')
                buffer += chunk
                sys.stdout.write(chunk)
                sys.stdout.flush()
                if pattern in buffer:
                    return buffer
            if proc.poll() is not None:
                break
        return buffer

    # Wait for xv6 boot prompt ($ )
    print("Waiting for boot...")
    read_until("$ ", timeout=8)
    
    print("\n\n>>> RUNNING klog_stress <<<\n")
    os.write(master, b"klog_stress\n")
    read_until("$ ", timeout=10)

    print("\n\n>>> RUNNING klog_test <<<\n")
    os.write(master, b"klog_test\n")
    read_until("$ ", timeout=10)

    print("\n\n>>> RUNNING klog_bench <<<\n")
    os.write(master, b"klog_bench\n")
    read_until("$ ", timeout=15)

    print("\n\n>>> RUNNING ulog_tool <<<\n")
    os.write(master, b"ulog_tool\n")
    read_until("$ ", timeout=10)

    # Exit QEMU: Ctrl-A then x
    os.write(master, b"\x01x")
    time.sleep(1)
    if proc.poll() is None:
        proc.terminate()
    os.close(master)

if __name__ == "__main__":
    run_xv6_interactive()
