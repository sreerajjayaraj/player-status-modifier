#include "hud_scanner.h"
#include "native_3446.h"
#include <Windows.h>
#include "scanner.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

void Log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    std::puts("");
    va_end(args);
}

int main(int argc, char** argv) {
    if (argc != 2 && argc != 3) { std::puts("usage: scanner-image-tests <1.0.0.3663 executable>"); return 2; }
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
    if (bytes.size() < 4096) return 2;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(bytes.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > bytes.size()) return 2;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(bytes.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.SizeOfHeaders > bytes.size()) return 2;
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, nt->OptionalHeader.SizeOfImage,
                                                         MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!image) return 2;
    std::memcpy(image, bytes.data(), nt->OptionalHeader.SizeOfHeaders);
    auto* sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& s = sections[i];
        if (static_cast<size_t>(s.PointerToRawData) + s.SizeOfRawData > bytes.size() ||
            static_cast<size_t>(s.VirtualAddress) + s.SizeOfRawData > nt->OptionalHeader.SizeOfImage) return 2;
        std::memcpy(image + s.VirtualAddress, bytes.data() + s.PointerToRawData, s.SizeOfRawData);
    }
    SetScannerTestModule(image); // Data-only map. Never load or execute the game.
    const auto base = reinterpret_cast<uintptr_t>(image);
    auto* mapped_nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
    int failures = 0;
    const auto check = [&failures](bool condition, const char* name) {
        std::printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
        if (!condition) ++failures;
    };
    // A metadata-only rebuild must keep every validated native route.
    mapped_nt->FileHeader.TimeDateStamp ^= 0xC0DE;
    mapped_nt->OptionalHeader.CheckSum ^= 0xFF;
    const auto item=ScanForItemGainAccess();
    std::printf("ITEM=%llX push=%llX plan=%llX owner-global=%llX\n",item.address?item.address-base:0,item.push_function?item.push_function-base:0,item.plan_site?item.plan_site-base:0,item.inventory_type_table_global?item.inventory_type_table_global-base:0);
    if(argc!=3)check(item.address!=0,"item native contract");
    const auto rate=ScanForNativeResourceRateAccess(),cost=ScanForNativeResourceCostAccess();
    check(ValidateNativeResourceGroup(rate,cost),"native resource group");
    const auto hud=psm::hud::ScanContracts(image,nt->OptionalHeader.SizeOfImage,cost);
    std::printf("HUD status=%d stage=%d gauge=%llX\n",int(hud.status),int(hud.stage),hud.gauge_update?hud.gauge_update-base:0);
    check(bool(hud),"hud linked contract");
    check(!hud.gauge_actor_rbp && hud.gauge_before_current_load==base+0x1050269 &&
          hud.visibility_after_call==base+0x121D74C && hud.cost_after_commit==base+0xB80A47A,
          "3663 HUD register allocation and instruction boundaries");
    check(psm::hud::ValidateHudModifierListContract(image,nt->OptionalHeader.SizeOfImage),"hud modifier contract");
    const auto mount=psm::hud::ScanMountedResourceContracts(image,nt->OptionalHeader.SizeOfImage);
    std::printf("MOUNT status=%d stage=%d\n",int(mount.status),int(mount.stage));
    check(bool(mount),"mount linked contract");
    check(mount.current_3446 && mount.mount_actor_capture==base+0x1217ED9 &&
          mount.query_after_lookup==base+0x194C854 && mount.entry_lookup==base+0x194CD30,
          "3663 mount owner/actor register and lookup boundaries");
    check(ScanForStatWriteAccess().address==base+0xB80DA53,
          "health value mutation precedes native death and state notifications");
    check(ScanForSpiritValueWriteAccess()==base+0xB80DA10,
          "shared writer resolver retains the proven 3663 entry point");
    for (const size_t key : {0xB80DA28u,0xB80DA52u,0xB80DA5Fu,0xB80DAA2u,0xB80DB41u,0x67BC250u}) {
      image[key]^=1;
      check(!ScanForStatWriteAccess().address&&!ValidateNativeResourceGroup(rate,cost)&&!psm::hud::ScanContracts(image,nt->OptionalHeader.SizeOfImage,cost),"native writer ABI and terminal drift rejected by all consumers");
      image[key]^=1;
    }
    check(!ValidateNativeResourceGroup(rate+1,cost) && !ValidateNativeResourceGroup(rate,cost+1),
          "misaligned resource group fails closed");
    psm::v3446::Image view{};check(view.Init(image,nt->OptionalHeader.SizeOfImage),"strict profile PE initialization");
    struct Mutation {size_t rva;const psm::v3446::Profile* profile;const char* name;};
    const Mutation mutations[]={
     {0x19534DA,&psm::v3446::kReader,"reader output capture register"},
     {0x19534E2,&psm::v3446::kReader,"reader metadata lookup edge"},
     {0x19534E9,&psm::v3446::kReader,"metadata classification field"},
     {0xB80A485,&psm::v3446::kCost,"recovery delay metadata plus48"},
     {0xB80DA25,&psm::v3446::kWriter,"writer encoded low-clamp address"},
     {0xB80DA56,&psm::v3446::kWriter,"writer death threshold field"},
     {0xB80DA60,&psm::v3446::kWriter,"writer terminal state byte"},
     {0xB80DB32,&psm::v3446::kWriter,"writer final stored value"},
     {0x121D654,&psm::v3446::kVisibility,"status dead byte293"},
     {0x121D77D,&psm::v3446::kVisibility,"HUD timer31C"},
     {0x104F788,&psm::v3446::kGauge,"gauge actor R15 capture"},
     {0x104F7D9,&psm::v3446::kGauge,"role byte255"},
     {0x105026C,&psm::v3446::kGauge,"gauge current cache230"},
     {0x1050300,&psm::v3446::kGauge,"gauge native notification538"},
     {0x121CB91,&psm::v3446::kRide,"native ride cache370"},
     {0x1217EB1,&psm::v3446::kRoot,"mount resolve reads matching ride cache"},
     {0x194C710,&psm::v3446::kQuery,"query timestamp capture"},
     {0x194C720,&psm::v3446::kQuery,"query metadata kind gate"},
     {0x194C853,&psm::v3446::kQuery,"query entry lookup link"},
     {0x194CDAA,&psm::v3446::kSlot,"entry lookup owner array58"},
     {0x194CDA6,&psm::v3446::kSlot,"canonical entry stride90"},
     {0x19528FC,&psm::v3446::kMutator,"modifier list pointerD8"},
     {0xB80FA78,&psm::v3446::kContribution,"negative penalty metadata18"},
     {0xB80FAD1,&psm::v3446::kContribution,"native contribution arithmetic"},
    };
    for(const auto&m:mutations){image[m.rva]^=1;check(!view.Find(*m.profile),m.name);image[m.rva]^=1;}
    image[0xB80A485]^=1;
    check(!ValidateNativeResourceGroup(rate,cost)&&!psm::hud::ScanContracts(image,view.size,cost),
          "recovery metadata drift disables group and HUD consumers");image[0xB80A485]^=1;
    image[0xB80DA25]^=1;
    check(!ScanForStatWriteAccess().address,"writer address decode drift disables health hook");image[0xB80DA25]^=1;
    const auto ex=mapped_nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    auto*records=reinterpret_cast<RUNTIME_FUNCTION*>(image+ex.VirtualAddress);
    RUNTIME_FUNCTION*chained=nullptr;
    for(size_t n=0;n<ex.Size/sizeof(*records);++n)if(records[n].BeginAddress==0x195352A)chained=&records[n];
    check(chained!=nullptr,"reader chained pdata fragment located");
    if(chained){const auto unwind=chained->UnwindData;const auto count=image[unwind+2];
      const auto at=unwind+4+((count+1u)&~1u)*2;image[at]^=1;
      check(!view.Find(psm::v3446::kReader),"CHAININFO must resolve exact canonical primary record");image[at]^=1;
      const auto saved=chained->BeginAddress;chained->BeginAddress=0x1953529;
      check(!view.Find(psm::v3446::kReader),"overlapping pdata ownership rejected");chained->BeginAddress=saved;
    }
    const size_t duplicate=0x1200000;
    std::vector<unsigned char>saved(image+duplicate,image+duplicate+psm::v3446::kVisibility.size);
    std::memcpy(image+duplicate,psm::v3446::kVisibility.code,psm::v3446::kVisibility.size);
    check(!view.Find(psm::v3446::kVisibility),"duplicate exact code rejected before boundary validation");
    std::memcpy(image+duplicate,saved.data(),saved.size());
    mapped_nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION].Size=0;
    check(!psm::hud::ScanContracts(image,view.size,cost)&&!psm::hud::ScanMountedResourceContracts(image,view.size),
          "missing pdata disables HUD and mount consumers");
    mapped_nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION]=ex;
    check(ValidateNativeResourceGroup(rate,cost)&&bool(psm::hud::ScanContracts(image,view.size,cost))&&
          bool(psm::hud::ScanMountedResourceContracts(image,view.size)),"all native mutation fixtures restored");
#define PRINT_SCAN(f) {const auto a=f();std::printf(#f "=%llX\n",a?a-base:0);}
    PRINT_SCAN(ScanForDamageBattleAccess); PRINT_SCAN(ScanForDragonFlyingRestrictWrite); PRINT_SCAN(ScanForDragonRoofRestrictTest); PRINT_SCAN(ScanForDragonVillageSummonJump);
    PRINT_SCAN(ScanForAbyssDurabilityDeltaAccess); PRINT_SCAN(ScanForAffinityGainPrepare); PRINT_SCAN(ScanForAffinityCurrentWrite); PRINT_SCAN(ScanForAffinityVaryFriendly); PRINT_SCAN(ScanForAffinityVaryFriendlyWithLogout);
    PRINT_SCAN(ScanForDurabilityDeltaAccess); PRINT_SCAN(ScanForDurabilityWriteAccess); PRINT_SCAN(ScanForPositionHeightAccess); PRINT_SCAN(ScanForSpiritDeltaAccess); PRINT_SCAN(ScanForSharedResourceDeltaAccess); PRINT_SCAN(ScanForSpiritValueWriteAccess); PRINT_SCAN(ScanForStaminaAb00Access); PRINT_SCAN(ScanForStatsAccess);
    std::printf("PLAYER=%llX STATWRITE=%llX\n",ScanForPlayerPointerCapture().address-base,ScanForStatWriteAccess().address-base);
    VirtualFree(image,0,MEM_RELEASE); return failures?1:0;
}