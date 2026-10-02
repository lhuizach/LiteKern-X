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
    record NAME             record the screen into OUTDIR/NAME.mp4 (real speed, 30 fps) ...
    stop                    ... until here
    filmclick NAME FRAMES MS   click, then film (the animation it starts)
    winrect APP             remember APP's window rectangle from "wm: open APP at x,y wxh"
                            as $x $y $w $h $r (right) $b (bottom) for later lines
    set VAR EXPR            a variable from a Python expression ($vars allowed)
Coordinates may use $vars and arithmetic: "to $r-24 $y+24".

Recording: build the image with -DANIM_SLOW=N and set LKX_SLOW=N here; every
pause in the script is then N times longer too, and the video is played back
N times faster, so it shows real speed with N times the frames. Encoding
needs an ffmpeg: LKX_FFMPEG (default: ffmpeg).
"""
import os, re, socket, struct, subprocess, sys, threading, time, zlib

sys.path.insert(0, os.path.dirname(__file__))
import qemu_monitor  # noqa: E402

START = (512, 384)          # the kernel puts the pointer at the screen's centre
SLOW = float(os.environ.get("LKX_SLOW", "1"))
FFMPEG = os.environ.get("LKX_FFMPEG", "ffmpeg")


def pause(seconds):
    time.sleep(seconds * SLOW)


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
    lock = threading.Lock()
    rec = {"on": False, "thread": None}

    def send(cmd):
        with lock:
            try:
                mon.sendall(cmd.encode() + b"\n")
            except OSError:             # the VM switched itself off
                rec["on"] = False

    def drain():                        # QEMU's replies: read and dropped
        try:
            while mon.recv(65536):
                pass
        except OSError:
            pass
    threading.Thread(target=drain, daemon=True).start()

    def grab(ppm):
        if os.path.exists(ppm):
            os.remove(ppm)
        send("screendump " + ppm)
        for _ in range(300):
            time.sleep(0.005)
            if os.path.exists(ppm) and os.path.getsize(ppm) >= 1024 * 768 * 3 + 15:
                return True
        return False

    def recorder(name):
        """Screendumps as fast as they come, streamed into ffmpeg at 30 fps of
        video time (real time / SLOW): each one fills the frames up to the next."""
        out = os.path.join(outdir, name + ".mp4")
        outw = subprocess.run(["wslpath", "-w", os.path.abspath(out)], capture_output=True,
                              text=True).stdout.strip() or out
        enc = subprocess.Popen([FFMPEG, "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
                                "-s", "1024x768", "-r", "30", "-i", "-", "-vf", "scale=800:-2",
                                "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "21",
                                "-movflags", "+faststart", outw], stdin=subprocess.PIPE)
        ppm = f"/tmp/lkx-rec-{os.getpid()}.ppm"
        t0, last, k, n = None, None, 0, 0
        while rec["on"]:
            t = time.time()
            if not grab(ppm):
                continue
            px = qemu_monitor.read_ppm(ppm)[2]
            if t0 is None:
                t0 = t
            v = (t - t0) / SLOW
            while last is not None and k / 30 < v:
                enc.stdin.write(last)
                k += 1
            last = px
            n += 1
        for _ in range(15):                 # hold the last picture half a second
            enc.stdin.write(last)
            k += 1
        enc.stdin.close()
        enc.wait()
        print(f"drive: {out}: {k / 30:.1f} s from {n} screendumps")

    def serial():
        return open(log, errors="replace").read()

    def wait(regex, secs=10):
        end = time.time() + secs * SLOW
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
            pause(0.02)
        pause(0.05)

    def shot(out):
        ppm = f"/tmp/lkx-drive-{os.getpid()}.ppm"
        grab(ppm)
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
            pause(0.1)
        elif op == "up":
            buttons = 0
            send("mouse_button 0")
            pause(0.1)
        elif op == "click":
            send("mouse_button 1")
            pause(0.08)
            send("mouse_button 0")
            pause(0.15)
        elif op == "key":
            for k in args:
                send("sendkey " + k)
                pause(0.12)
        elif op == "sleep":
            pause(float(args[0]))
        elif op == "record":
            rec["on"] = True
            rec["thread"] = threading.Thread(target=recorder, args=(args[0],))
            rec["thread"].start()
        elif op == "stop":
            rec["on"] = False
            rec["thread"].join()
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
