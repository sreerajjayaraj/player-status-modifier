"""Read-only native item function/CHAININFO analysis for the fingerprinted 3446 PE."""
import sys,struct,bisect
from pathlib import Path
import pefile,capstone
P=Path(r"D:/SteamLibrary/steamapps/common/Crimson Desert/bin64/CrimsonDesert.exe")
p=pefile.PE(str(P),fast_load=True)
image=bytes(p.get_memory_mapped_image(max_virtual_address=0x40000000))
d=p.OPTIONAL_HEADER.DATA_DIRECTORY[3]
records=list(struct.iter_unpack("<III",image[d.VirtualAddress:d.VirtualAddress+d.Size]))
starts=[r[0] for r in records]
by_start={r[0]:r for r in records}
cache={}
def canonical(r,seen=()):
    if r in cache:return cache[r]
    if r[2] in seen:raise ValueError("unwind cycle")
    h=image[r[2]:r[2]+4]
    if h[0]>>3&4:
        at=r[2]+4+((h[2]+1)&~1)*2
        chain=struct.unpack_from("<III",image,at)
        if by_start.get(chain[0])!=chain:raise ValueError("invalid chain")
        root=canonical(chain,seen+(r[2],))
    else:root=r
    cache[r]=root
    return root

def owner(at):
    n=bisect.bisect_right(starts,at)-1
    if n<0 or at>=records[n][1]:raise ValueError(f"no owner {at:X}")
    return canonical(records[n])

def profile(at):
    root=owner(at)
    fragments=[]
    for r in records:
        try:
            if canonical(r)==root:fragments.append(r)
        except ValueError:pass
    end=max(r[1] for r in fragments)
    return root[0],end,fragments

def follow(at):
    seen=set()
    while image[at]==0xE9:
        if at in seen:raise ValueError("thunk cycle")
        seen.add(at);at=at+5+struct.unpack_from("<i",image,at+1)[0]
    return at

if __name__=="__main__":
    out=Path(__file__).resolve().parents[1]/"evidence/item-native"
    out.mkdir(parents=True,exist_ok=True)
    md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64)
    for arg in sys.argv[1:]:
        at=int(arg,0);begin,end,fragments=profile(at)
        lines=[];calls=[]
        for ins in md.disasm(image[begin:end],begin):
            lines.append(f"{ins.address:08X} {ins.bytes.hex(' '):35} {ins.mnemonic:10} {ins.op_str}")
            if ins.mnemonic=="call" and len(ins.bytes)==5 and ins.bytes[0]==0xE8:
                target=ins.address+5+struct.unpack_from("<i",ins.bytes,1)[0]
                calls.append(f"+{ins.address-begin:X}:{target:X}->{follow(target):X}")
        (out/f"{begin:08X}.txt").write_text("\n".join(lines)+"\n")
        print(f"OWNER {begin:X}..{end:X} fragments={len(fragments)}",calls)
