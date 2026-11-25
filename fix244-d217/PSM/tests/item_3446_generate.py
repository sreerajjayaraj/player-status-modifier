"""Generate immutable item3446 code/fragment profiles from the verified inert PE."""
from pathlib import Path
import hashlib,json
import item_3446_analyze as a
root=Path(__file__).resolve().parents[1]
assert hashlib.sha256(a.P.read_bytes()).hexdigest().upper()=="FEC07BE1B725A46624D31FCD74BF3730EA809765226CEA6BDE01607AB2D79F66"
nodes=[("Receipt",0xD937AA0,None),("Push",0x29E3310,None),("Plan",0x29E2CE0,None),
("Standalone",0x2DE61C0,None),("Owner",0x22EB0A0,None),("Metadata",0x3DCB40,None),
("Capacity",0xD9724A0,0x72),("Merge",0xD95C540,None),("Copy",0xD944930,None),
("Rows",0xE98A110,None),("SourceInit",0x29E4CE0,None),("SourcePlan",0x29E4E00,None),
("SourceGroup",0x22F05B0,None),("SourceConsumer",0x25F5220,None),("Pop",0x29E52D0,None),
("SqlGate",0x29E8FC0,None),("Mapper",0x1C23510,0x6CC),("SourceRemove",0xCD54A40,None),
("DestinationGroup",0x22EFD40,None)]
lines=['// Exact immutable item code contracts for verified 1.0.0.3446 PE. No RVA fallback.','#pragma once','#include "native_3446.h"','namespace psm::items3446 {','using psm::v3446::Profile;','using psm::v3446::Fragment;']
manifest=[]
for name,at,length in nodes:
    if length:begin,end,frags=at,at+length,[]
    else:begin,end,frags=a.profile(at)
    blob=a.image[begin:end]
    lines.append(f'inline constexpr unsigned char k{name}Code[]={{')
    lines.extend(' '+','.join(f'0x{x:02X}' for x in blob[i:i+24])+',' for i in range(0,len(blob),24))
    lines.append('};')
    if frags:
        lines.append(f'inline constexpr Fragment k{name}Fragments[]={{')
        lines.extend(f' {{0x{r[0]-begin:X},0x{r[1]-begin:X},0x{a.canonical(r)[0]-begin:X}}},' for r in frags)
        lines.append('};')
        lines.append(f'inline constexpr Profile k{name}{{k{name}Code,sizeof(k{name}Code),k{name}Fragments,std::size(k{name}Fragments)}};')
    else:lines.append(f'inline constexpr Profile k{name}{{k{name}Code,sizeof(k{name}Code),nullptr,0}};')
    manifest.append(dict(name=name,rva=hex(begin),size=len(blob),sha256=hashlib.sha256(blob).hexdigest().upper(),fragments=[dict(begin=hex(r[0]),end=hex(r[1]),root=hex(a.canonical(r)[0])) for r in frags]))
lines.append('}')
(root/'src/item_3446_profiles.h').write_text('\n'.join(lines)+'\n',encoding='utf-8')
(root/'evidence/item3446-profiles.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print('Generated',len(nodes),'strict native item profiles; bytes=',sum(x['size'] for x in manifest))
