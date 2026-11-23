import subprocess, time, sys
import os, tempfile
A=["adb"]; D="/dev/input/event9"
CODES={"left":(3,16,-1),"right":(3,16,1),"up":(3,17,-1),"down":(3,17,1)}
KEYS={"a":304,"jump":304,"b":305,"y":308,"shoot":307,"R":311,"select":314,"start":315}
def adb_sh(s): subprocess.run(A+["shell",s])
def ev(t,c,v): return "sendevent %s %d %d %d"%(D,t,c,v)
SYN=ev(0,0,0)
def hold(name,s):
    if name in CODES:
        t,c,v=CODES[name]; adb_sh("%s; %s; sleep %s; %s; %s"%(ev(t,c,v),SYN,s,ev(t,c,0),SYN))
    else:
        k=KEYS[name]; adb_sh("%s; %s; sleep %s; %s; %s"%(ev(1,k,1),SYN,s,ev(1,k,0),SYN))
def seq(parts):
    """parts: list of (cmd, arg): ('dn',name) ('up',name) ('sl',sec)"""
    out=[]
    for p,a in parts:
        if p=='sl': out.append("sleep %s"%a)
        else:
            if a in CODES: t,c,v=CODES[a]; val=v if p=='dn' else 0; out.append(ev(t,c,val))
            else: out.append(ev(1,KEYS[a],1 if p=='dn' else 0))
            out.append(SYN)
    adb_sh("; ".join(out))
def state():
    time.sleep(1.3)
    o=subprocess.run(A+["shell","cat","/sdcard/Azahar/sdmc/SMZ3/debug_state.txt"],capture_output=True,text=True).stdout.split()
    return [int(x) for x in o][:6] if len(o)>=6 else None
def step(label,fn,*a):
    fn(*a); print(label.ljust(26),state(),flush=True)
def door():
    hold("left",9)
    seq([('dn','jump'),('sl',0.3),('dn','shoot'),('sl',0.3),('up','shoot'),('sl',0.4),('up','jump'),('sl',1.2)])
def rest():
    for name,s in [("left",4),("sl",1),("left",5),("shoot",0.2),("left",1),("sl",2),("left",5),("down",0.4),("sl",0.3),("down",0.4),("sl",0.5),("left",0.5),("shoot",0.2),("sl",1.5),("left",2),("shoot",0.2),("sl",1.5),("left",3),("shoot",0.2),("sl",0.4),("shoot",0.2),("sl",2),("left",4),("shoot",0.2),("sl",1.5),("left",3),("left",1),("sl",5),("right",1.5),("sl",6),("left",1),("sl",5),("right",2),("sl",6),("right",2),("sl",3),("left",1.5),("sl",4),("right",2),("sl",3),("right",2),("sl",3)]:
        if name=="sl": time.sleep(s); continue
        hold(name,s); print(("%s %s"%(name,s)).ljust(26),state(),flush=True)
def routeA():
    step("up",hold,"up",0.3); time.sleep(0.5); step("shoot",hold,"shoot",0.3); time.sleep(1)
    step("right6",hold,"right",6); step("right6",hold,"right",6)
    seq([('dn','select'),('up','select')]); time.sleep(0.5)
    seq([('dn','right'),('sl',0.2),('dn','jump'),('sl',0.9),('up','jump'),('sl',0.7),('up','right')]); print("jump-right",state())
    for i in range(6): hold("shoot",0.2); time.sleep(0.8)
    time.sleep(2); step("right5",hold,"right",5); time.sleep(3); step("right5",hold,"right",5); time.sleep(2); print(state())
def routeB():
    step("right3",hold,"right",3); time.sleep(1)
    seq([('dn','R'),('sl',0.3),('dn','shoot'),('sl',0.2),('up','shoot'),('sl',0.6),('dn','shoot'),('sl',0.2),('up','shoot'),('sl',0.3),('up','R')]); time.sleep(3); print("orb",state())
def routeC():
    seq([('dn','right'),('sl',0.15),('dn','jump'),('sl',0.3),('up','jump'),('sl',1),('up','right')]); time.sleep(3); print("touch",state())
if __name__=="__main__":
    for f in sys.argv[1:]: globals()[f]()
def raw():
    time.sleep(.9); return subprocess.run(A+["shell","cat","/sdcard/Azahar/sdmc/SMZ3/debug_state.txt"],capture_output=True,text=True).stdout.strip()
def to_shaft():
    door(); hold("left",3); hold("left",4)
    seq([('dn','left'),('dn','jump'),('sl',0.35),('dn','shoot'),('sl',0.2),('up','shoot'),('sl',0.3),('up','jump'),('sl',0.4),('up','left')])
    for i in range(4): hold("left",1.5)
    hold("right",0.6); hold("right",0.6); hold("right",0.6); time.sleep(4); hold("right",0.5); time.sleep(2); hold("right",1)
def dump():
    A_=A; subprocess.run(A_+["shell","touch /sdcard/Azahar/sdmc/SMZ3/debug_dump.txt"]); time.sleep(2.5)
    subprocess.run(A_+["pull","/sdcard/Azahar/sdmc/SMZ3/ram.bin",os.path.join(tempfile.gettempdir(), "ram.bin")],capture_output=True)
    r=open(os.path.join(tempfile.gettempdir(), "ram.bin"),"rb").read()
    w=lambda o:r[o]|r[o+1]<<8
    return dict(mod=r[0x10],sub=r[0x11],room=w(0xA0),indoors=r[0x1B],x=w(0x22),y=w(0x20),menu=r[0x200],cur=r[0x0202] if False else None,hearts=r[0x7EF36D-0x7E0000],rup=w(0x7EF360-0x7E0000),chest=w(0x7EF208-0x7E0000))
