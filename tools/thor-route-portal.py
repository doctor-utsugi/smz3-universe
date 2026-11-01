"""From the Samus landing site (SM, just after START GAME) to the Crateria door that holds the Zelda portal, in the Thor build.
Run: python3 tools/thor-route-portal.py   Prints the tracker state after each step; ends at SM room 92fd x=475 y=912."""
import importlib.util as u, time, os
s = u.spec_from_file_location("t", os.path.join(os.path.dirname(__file__), "thor-input.py")); t = u.module_from_spec(s); s.loader.exec_module(t)
t.step("left9", t.hold, "left", 9)
t.seq([("dn", "jump"), ("sl", 0.3), ("dn", "shoot"), ("sl", 0.3), ("up", "shoot"), ("sl", 0.4), ("up", "jump"), ("sl", 1.2)])
t.step("left3", t.hold, "left", 3); t.step("left4", t.hold, "left", 4)
t.seq([("dn", "left"), ("dn", "jump"), ("sl", 0.35), ("dn", "shoot"), ("sl", 0.2), ("up", "shoot"), ("sl", 0.3), ("up", "jump"), ("sl", 0.4), ("up", "left")])
for i in range(4): t.step("left1.5", t.hold, "left", 1.5)
for i in range(3): t.step("right.6", t.hold, "right", 0.6)
time.sleep(4); t.step("right.5", t.hold, "right", 0.5); time.sleep(2); t.step("right1", t.hold, "right", 1)
