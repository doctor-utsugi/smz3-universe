"""Key injection for the Lemuroid build on the AYN Thor (adb sendevent on the Odin controller) and the game state
the tracker logs ("SMZ3 game=..." lines: room, Samus x/y, ALttP module/room/x/y). Android mapping on the Thor: jump=305, dash=304, shoot=308, cancel=307 (Super Metroid layout: Y shoots, B jumps, A dashes, X cancels; see Smz3Tracker.remap)."""
import subprocess, time, re
A = ["adb"]; D = "/dev/input/event9"
HAT = {"left": (3, 16, -1), "right": (3, 16, 1), "up": (3, 17, -1), "down": (3, 17, 1)}
KEY = {"jump": 305, "dash": 304, "shoot": 308, "cancel": 307, "L": 310, "R": 311, "select": 314, "start": 315}
def sh(s): subprocess.run(A + ["shell", s])
def ev(t, c, v): return "sendevent %s %d %d %d; sendevent %s 0 0 0" % (D, t, c, v, D)
def code(n, v):
    if n in HAT: t, c, val = HAT[n]; return ev(t, c, val if v else 0)
    return ev(1, KEY[n], v)
def hold(n, s): sh("%s; sleep %s; %s" % (code(n, 1), s, code(n, 0)))
def seq(parts):
    out = []
    for p, a in parts:
        out.append("sleep %s" % a if p == "sl" else code(a, 1 if p == "dn" else 0))
    sh("; ".join(out))
def state():
    time.sleep(1.2)
    o = subprocess.run(A + ["logcat", "-d", "-t", "400"], capture_output=True, text=True).stdout
    m = re.findall(r"SMZ3 game=(\d) started=(\w+) .*? nmi=(\d+) room=(\w+) x=(\d+) y=(\d+) zmod=(\w+) zroom=(\w+) zx=(\d+) zy=(\d+)", o)
    return m[-1] if m else None
def step(label, fn, *a):
    fn(*a); print(label.ljust(22), state(), flush=True)
def portal_trip(back=True):
    """From SM room 92fd next to the Zelda portal door: turn right (after a return Samus stands facing LEFT, pose 0002,
    so a shot would miss the door), shoot the door open, hold right into Zelda and optionally walk up back to SM."""
    hold("right", 0.15); time.sleep(0.3)
    hold("shoot", 0.3); time.sleep(1.2)
    hold("right", 3); time.sleep(4); z = state()
    if back: hold("up", 2.5); time.sleep(4)
    return z, state()
