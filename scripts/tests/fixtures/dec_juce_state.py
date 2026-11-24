# The decoder of the host-preset investigation (.hw-local/mpc-perf/host-presets/dec.py, 2026-11-14), unchanged: JUCE base64 <state> of an .xpl -> the decoded bytes (argv[2]).
# The generator's files must decode with it to the same structure as the files the device wrote (scripts/tests/test_gen_mpc_xpl.py).
import re,sys
A=".ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+"
def dec(s):
    n,rest=s.split('.',1); n=int(n)
    bits=0; nb=0; out=bytearray()
    for c in rest:
        v=A.index(c); bits|=v<<nb; nb+=6
        while nb>=8: out.append(bits&255); bits>>=8; nb-=8
    return bytes(out[:n]),n
t=open(sys.argv[1],encoding='utf-8',errors='replace').read()
m=re.search(r'<state>(.*?)</state>',t,re.S); d,n=dec(m.group(1).strip())
open(sys.argv[2],'wb').write(d); print(n,len(d)); print(d[:160])
