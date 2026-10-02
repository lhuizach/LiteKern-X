#!/usr/bin/env python3
"""LiteKern X - drive the OS in a headless QEMU with absolute pointer moves,
for screenshots, demo recordings and debugging (the tests use
tests/lib/qemu_monitor.py directly).

    python3 tests/lib/drive.py IMAGE OUTDIR SCRIPT

SCRIPT is a file of steps, one per line ('#' starts a comment):
    wait REGEX              until a serial line matches (10 s)
    to X Y                  move the pointer to screen (X, Y), in small steps
    jump X Y                move it there in one go
    down / up / click       left button
    key NAME...             QEMU sendkey names
    sleep SECONDS
    shot NAME               OUTDIR/NAME.png
    film NAME FRAMES MS     FRAMES screenshots MS apart: OUTDIR/NAME-000.png ...
    filmclick NAME FRAMES MS   click, then film (the animation it starts)
    winrect APP             remember APP's window rectangle from "wm: open APP at x,y wxh"
                            as $x $y $w $h $r (right) $b (bottom) for later lines
    set VAR EXPR            a variable from a Python expression ($vars allowed)
Coordinates may use $vars and arithmetic: "to $r-24 $y+24".
"""
import os, re, socket, struct, subprocess, sys, time, zlib

sys.path.insert(0, os.path.dirname(__file__))
import qemu_monitor  # noqa: E402

START = (512, 384)          # the kernel puts the pointer at the screen's centre


def main():
    img, outdir, script = sys.argv[1:4]
    os.makedirs(outdir, exist_ok=True)
    sock = f"/tmp/lkx-drive-{os.getpid()}.sock"
    log = f"/tmp/lkx-drive-{os.getpid()}.log"
    if os.path.exists(sock):
        os.remove(sock)
    logf = open(log, "w")
    qemu = subprocess.Popen(["bash", "vm/qemu.sh", "--headless", "--image", img, "--",
                             "-monitor", f"unix:{sock},server,nowait"], stdout=logf, stderr=subprocess.STDOUT)
    for _ in range(100):
        if os.path.exists(sock):
            break
        time.sleep(0.1)
    time.sleep(0.2)
    for _ in range(50):                 # the socket appears just before QEMU listens
        mon = socket.socket(socket.AF_UNIX)
        try:
            mon.connect(sock)
            break
        except OSError:
            mon.close()
            time.sleep(0.1)
    pos = list(START)
    buttons = 0
    vars_ = {}

    def send(cmd):
        mon.sendall(cmd.encode() + b"\n")

    def serial():
        return open(log, errors="replace").read()

    def wait(regex, secs=10):
        end = time.time() + secs
        while time.time() < end:
            if re.search(regex, serial(), re.M):
                return True
            time.sleep(0.05)
        print(f"drive: timed out waiting for {regex}", file=sys.stderr)
        return False

    def ev(expr):
        for k in sorted(vars_, key=len, reverse=True):
            expr = expr.replace("$" + k, str(vars_[k]))
        return int(eval(expr))

    def move(x, y, steps):
        nonlocal pos
        for i in range(1, steps + 1):
            tx = pos[0] + (x - pos[0]) // (steps - i + 1)
            ty = pos[1] + (y - pos[1]) // (steps - i + 1)
            send(f"mouse_move {tx - pos[0]} {ty - pos[1]}")
            pos = [tx, ty]
            time.sleep(0.02)
        time.sleep(0.05)

    def shot(out):
        ppm = f"/tmp/lkx-drive-{os.getpid()}.ppm"
        if os.path.exists(ppm):
            os.remove(ppm)
        send("screendump " + ppm)
        for _ in range(100):
            time.sleep(0.01)
            if os.path.exists(ppm) and os.path.getsize(ppm) >= 1024 * 768 * 3:
                break
        time.sleep(0.02)
        w, h, px = qemu_monitor.read_ppm(ppm)
        rows = b"".join(b"\0" + px[y * w * 3:(y + 1) * w * 3] for y in range(h))
        chunk = lambda t, b: struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b))
        with open(out, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                    + chunk(b"IDAT", zlib.compress(rows, 1)) + chunk(b"IEND", b""))
        os.remove(ppm)

    def film(name, frames, ms):
        for i in range(frames):
            t = time.time()
            shot(os.path.join(outdir, f"{name}-{i:03d}.png"))
            left = ms / 1000 - (time.time() - t)
            if left > 0:
                time.sleep(left)

    for raw in open(script):
        line = raw.split("#")[0].strip()
        if not line:
            continue
        op, *args = line.split()
        if op == "wait":
            wait(" ".join(args))
        elif op in ("to", "jump"):
            move(ev(args[0]), ev(args[1]), 12 if op == "to" else 1)
        elif op == "down":
            buttons = 1
            send("mouse_button 1")
            time.sleep(0.1)
        elif op == "up":
            buttons = 0
            send("mouse_button 0")
            time.sleep(0.1)
        elif op == "click":
            send("mouse_button 1")
            time.sleep(0.08)
            send("mouse_button 0")
            time.sleep(0.15)
        elif op == "key":
            for k in args:
                send("sendkey " + k)
                time.sleep(0.12)
        elif op == "sleep":
            time.sleep(float(args[0]))
        elif op == "shot":
            time.sleep(0.15)
            shot(os.path.join(outdir, args[0] + ".png"))
        elif op == "film":
            film(args[0], int(args[1]), int(args[2]))
        elif op == "filmclick":
            send("mouse_button 1")
            time.sleep(0.05)
            send("mouse_button 0")
            film(args[0], int(args[1]), int(args[2]))
        elif op == "winrect":
            m = re.findall(rf"^wm: open {re.escape(args[0])} at (-?\d+),(-?\d+) (\d+)x(\d+)", serial(), re.M)
            if m:
                x, y, w, h = map(int, m[-1])
                vars_.update(x=x, y=y, w=w, h=h, r=x + w, b=y + h)
        elif op == "set":
            vars_[args[0]] = ev(" ".join(args[1:]))
        else:
            print(f"drive: unknown step {op}", file=sys.stderr)
    _ = buttons
    qemu.kill()
    qemu.wait()
    out = open(log, errors="replace").read().replace("\r", "")
    open(os.path.join(outdir, "serial.log"), "w").write(out)
    os.remove(log)


if __name__ == "__main__":
    main()
