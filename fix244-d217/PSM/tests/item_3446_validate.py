"""Independent read-only verification of item3446 native code/profile/call contracts."""
import re,json,hashlib,struct
from pathlib import Path
import item_3446_analyze as a
root=Path(__file__).resolve().parents[1]
checks=0
def require(value,name):
    global checks
    if not value:raise AssertionError(name)
    checks+=1;print('PASS',name)
require(hashlib.sha256(a.P.read_bytes()).hexdigest().upper()=='FEC07BE1B725A46624D31FCD74BF3730EA809765226CEA6BDE01607AB2D79F66','fingerprinted current executable')
manifest=json.loads((root/'evidence/item3446-profiles.json').read_text())
positions={}
for node in manifest:
    begin=int(node['rva'],0);blob=a.image[begin:begin+node['size']]
    require(hashlib.sha256(blob).hexdigest().upper()==node['sha256'],node['name']+' immutable native code fingerprint')
    positions[re.sub(r'(?<!^)(?=[A-Z])','_',node['name']).lower()]=begin
    if node['fragments']:
        actual_begin,actual_end,fragments=a.profile(begin)
        require(actual_begin==begin and actual_end==begin+node['size'] and
                [dict(begin=hex(r[0]),end=hex(r[1]),root=hex(a.canonical(r)[0])) for r in fragments]==node['fragments'],
                node['name']+' exact canonical unwind ownership')
positions['transfer']=positions['standalone']
positions['sql']=positions['sql_gate']
text=(root/'src/item_3446.h').read_text()
for owner,offset,destination in re.findall(r'target\((\w+)\+(0x[0-9A-Fa-f]+)\)!=(\w+)',text):
    call=positions[owner]+int(offset,16)
    target=call+5+struct.unpack_from('<i',a.image,call+1)[0]
    require(a.image[call]==0xE8 and a.follow(target)==positions[destination],f'{owner}+{offset} links {destination}')
def exact(at,text,name):require(a.image[at:at+len(bytes.fromhex(text))]==bytes.fromhex(text),name)
exact(0xD937AAD,'45 0F BF E1 4C 89 C5 49 89 D7 48 89 CE 44 89 E7 66 44 39 61 08','receipt entry preserves signed R9W slot/R8 input/RDX error/RCX container ABI')
exact(0x3DCB53,'8B FA','compact metadata key captured from EDX')
exact(0xD9724A0,'4C 8B 49 10 49 89 CA 49 83 F9 01 7F 61','native stack greater than one bypass returns info+10')
exact(0xD97250E,'4C 89 C8 C3','native stackable capacity returned unchanged')
exact(0xD937BC5,'80 78 1D 00','weighted path tests compact metadata+1D')
exact(0x29E34E1,'8B 14 3E 3B 50 08','debt check compares compact metadata item ID+8')
exact(0x29E2DC6,'48 8B 4F 28 48 8D 94 24 80 00 00 00 4D 8B C6','planner pre-SQL callback uses captured push and E0 receipts')
exact(0x2DE776D,'8B 18 85 DB 75 1D 39 5F 60 74 15','committed source-success and nonempty-destination gates preserved')
exact(0xE98A1F1,'48 8B 8B E0 00 00 00 48 8B 47 10 48 89 04 0E','SQL rows use delivered receipt quantity+10')
exact(0xE98A2EE,'49 81 C5 E0 00 00 00','SQL receipts advance E0 stride')
exact(0x29E4D42,'4C 89 41 10 49 8B 39 41 8B 41 08 48 8D 0C 80 4C 8D 34 CF','pending-source vector has28-byte records and unchanged actor field')
exact(0x25F528B,'4D 8B 6E 18 4D 85 ED','native source consumer reads pending quantity+18')
exact(0x1C23510,'4C 8B C9 66 83 FA 1A','native mapper preserves RCX output ABI and accepts27 routes')
require('ItemMetadataKind::KeyEdx3446,0x10,0x1D,0x08' in text,'resolver exports compact profile tuple')
print(f'item native independent checks={checks} failures=0')
