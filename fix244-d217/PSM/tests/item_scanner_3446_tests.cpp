#include <Windows.h>
#include "scanner.h"
#include "item_3446.h"

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
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if (argc != 2) { std::puts("usage: scanner-image-tests <1.0.0.3663 executable>"); return 2; }
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
const auto reloc=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
 const auto delta=reinterpret_cast<uintptr_t>(image)-nt->OptionalHeader.ImageBase;
 for(size_t at=reloc.VirtualAddress;reloc.Size&&at<reloc.VirtualAddress+reloc.Size;){auto*block=reinterpret_cast<IMAGE_BASE_RELOCATION*>(image+at);if(block->SizeOfBlock<8)break;auto*fix=reinterpret_cast<uint16_t*>(block+1);for(unsigned j=0;j<(block->SizeOfBlock-8)/2;++j)if((fix[j]>>12)==IMAGE_REL_BASED_DIR64)*reinterpret_cast<uintptr_t*>(image+block->VirtualAddress+(fix[j]&0xFFF))+=delta;at+=block->SizeOfBlock;}
    SetScannerTestModule(image); // Data-only map. Never load or execute the game.
    const auto base = reinterpret_cast<uintptr_t>(image);
    int failures = 0;
    const auto check = [&failures](bool condition, const char* name) {
        std::printf("%s: %s\n", condition ? "PASS" : "FAIL", name);
        if (!condition) ++failures;
    };
    psm::v3446::Image probe;
    if(!probe.Init(image,nt->OptionalHeader.SizeOfImage)) return 2;
    using namespace psm::items3446;
    const psm::v3446::Profile* contracts[]={&kReceipt,&kPush,&kPlan,&kStandalone,&kOwner,&kMetadata,&kCapacity,&kMerge,&kCopy,&kRows,&kSourceInit,&kSourcePlan,&kSourceGroup,&kSourceConsumer,&kPop,&kSqlGate,&kMapper,&kSourceRemove,&kDestinationGroup};
    const char* names[]={"receipt","push","plan","transfer","owner","metadata","capacity","merge","copy","rows","source-init","source-plan","source-group","source-consumer","pop","sql","mapper","source-remove","destination-group"};
    for(size_t i=0;i<std::size(contracts);++i) std::printf("profile %s=%zX\n",names[i],probe.Find(*contracts[i],i!=6&&i!=16));
    const auto item = ScanForItemGainAccess();
    check(item.address == base + 0xDC6F5E0, "3663 item receipt resolves the native destination");
    std::printf("item address=%llX push=%llX plan=%llX caller=%llX mapper=%llX\n",
        item.address ? item.address-base:0, item.push_function ? item.push_function-base:0,
        item.plan_site ? item.plan_site-base:0, item.plan_caller_return ? item.plan_caller_return-base:0,
        item.container_mapper ? item.container_mapper-base:0);

    check(item.push_function == base + 0x2A4E360 && item.plan_site == base + 0x2A4DE16 &&
          item.plan_caller_return == base + 0x2E58E90 && item.standalone_caller_return == base + 0x2E58F71 &&
          item.container_mapper == base + 0x1C8CE80 && item.native_plan_function == base + 0x2A4DD30 && item.native_inventory_owner_lookup == base + 0x23626F0, "3663 storage planning and committed transfer stay linked");
    check(item.metadata_kind==ItemMetadataKind::PointerRcx3663 && item.stack_size_offset==0x10 &&
          item.weighted_offset==0x1D && item.item_id_offset==8,
          "current resolver exports the exact compact metadata ABI and layout to runtime");
    if(!item.address) return 1;
    const struct { size_t rva; const char* label; } mutations[] = {
        {0x2A4E831,"destination receipt relative call drift rejected"},
        {0x266FC59,"borrowed C8 constructor quantity-copy drift rejected"},
        {0x266CB23,"native capacity item-type ABI drift rejected"},
        {0x2373A06,"owned E0 append-copy drift rejected"},
        {0x2367847,"borrowed input assignment relationship drift rejected"},
        {0xDC6F5ED,"receipt slot R9W ABI drift rejected"},
        {0xDC6F5F4,"receipt error pointer capture rejected"},
        {0xDC6F67D,"C8 destination stride drift rejected"},
        {0xDC6F774,"merge relation drift rejected"},
        {0xDC6F7E8,"new-stack relation drift rejected"},
        {0xDC6F6BD,"metadata lookup relationship rejected"},
        {0x3DAF23,"item-key RCX pointer lookup ABI drift rejected"},
        {0xDCB7A63,"compact stack-capacity field drift rejected"},
        {0xDCB7A6B,"stackable capacity bypass drift rejected"},
        {0xDC6F718,"compact weighted flag drift rejected"},
        {0x2A4E535,"compact item ID debt field drift rejected"},
        {0x2362731,"owner classification field drift rejected"},
        {0x2A4DD45,"plan frame size drift rejected"},
        {0x2A4DD4D,"plan push capture drift rejected"},
        {0x2A4DDA1,"plan receipt descriptor drift rejected"},
        {0x2A4DE14,"nonempty receipt gate drift rejected"},
        {0x2A4DE25,"committed SQL row builder relation rejected"},
        {0xECD8560,"SQL row builder register ABI rejected"},
        {0x1C8CE82,"mapper output pointer register drift rejected"},
        {0x1C8CE86,"mapper accepted route bound drift rejected"},
        {0x1C8D4E0,"mapper table route drift rejected"},
        {0x2E58E31,"pending source preparation relation rejected"},
        {0x2E58F52,"source removal relation drift rejected"},
        {0x2E58F5B,"source success gate drift rejected"},
        {0x2E58F6C,"committed push relationship drift rejected"},
        {0x2A4FE00,"source pending-vector ABI drift rejected"},
        {0x2367F30,"source grouped-container ABI drift rejected"},
        {0x266D2A0,"source consumer ABI drift rejected"}
    };    for (const auto& mutation : mutations) {
        const auto old = image[mutation.rva];
        image[mutation.rva] ^= 1;
        const auto rejected = ScanForItemGainAccess();
        check(!rejected.address && !rejected.plan_site && !rejected.container_mapper, mutation.label);
        image[mutation.rva] = old;
    }
    for (const size_t key : {0xDC6F60Cu,0xDC6F67Cu,0xDC6F785u,0xDC807D4u,0x8AB662Cu}) {
        image[key]^=1;
        check(!psm::items3446::Resolve(image).address,"native divide, stride, tag or terminal key drift rejected");
        image[key]^=1;
    }
    check(probe.Find(kSourceRemove)!=0,"source-pop native fragments resolve");
    constexpr size_t source_chain = 0x68BF958 + 12;
    image[source_chain] ^= 1;
    check(!psm::items3446::Resolve(image).address,"changed source-pop unwind chain fails closed");
    image[source_chain] ^= 1;
    // Exercise the metadata equivalence predicate independently, preserving
    // every byte in these private, non-executed scratch fixtures afterwards.
    unsigned char scratch_a[12]{},scratch_b[12]{};
    std::memcpy(scratch_a,image+0x6000,12);std::memcpy(scratch_b,image+0x6100,12);
    std::memcpy(image+0x6000,image+0x6507CF8,12);
    std::memcpy(image+0x6100,image+0x6507CF8,12);
    check(probe.EquivalentLeafUnwind(0x6000,0x6100),"complete identical leaf unwind metadata accepted");
    for(const size_t field : {size_t{0},size_t{1},size_t{2},size_t{3},size_t{4}}){
        image[0x6000+field]^=1;
        check(!probe.EquivalentLeafUnwind(0x6000,0x6100),"unwind version prolog count frame or code mismatch rejected");
        image[0x6000+field]^=1;
    }
    for(const unsigned char flags : {static_cast<unsigned char>(8),static_cast<unsigned char>(16),static_cast<unsigned char>(32)}){
        image[0x6000]|=flags;image[0x6100]|=flags;
        check(!probe.EquivalentLeafUnwind(0x6000,0x6100),"EH UH and CHAIN metadata rejected even when identical");
        image[0x6000]&=~flags;image[0x6100]&=~flags;
    }
    image[0x6002]=3;image[0x6102]=3;
    check(probe.EquivalentLeafUnwind(0x6000,0x6100),"odd unwind code count includes aligned padding");
    image[0x600B]^=1;
    check(!probe.EquivalentLeafUnwind(0x6000,0x6100),"complete unwind padding mismatch rejected");
    image[0x600B]^=1;
    check(!probe.EquivalentLeafUnwind(0x6001,0x6100)&&
          !probe.EquivalentLeafUnwind(probe.size-2,0x6100)&&
          !probe.EquivalentLeafUnwind(0x6000,probe.size+4),"unaligned and out-of-image unwind metadata rejected");
    unsigned char tail[4]{};std::memcpy(tail,image+probe.size-4,4);
    std::memcpy(image+probe.size-4,image+0x6000,4);
    check(!probe.EquivalentLeafUnwind(probe.size-4,0x6100),"in-image header with truncated unwind codes rejected");
    std::memcpy(image+probe.size-4,tail,4);
    std::memcpy(image+0x6000,scratch_a,12);std::memcpy(image+0x6100,scratch_b,12);
    auto* current_nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
    const auto exception=current_nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
    auto* native_records=reinterpret_cast<RUNTIME_FUNCTION*>(image+exception.VirtualAddress);
    RUNTIME_FUNCTION* native_primary=nullptr;
    for(size_t n=0;n<exception.Size/sizeof(*native_records);++n)
        if(native_records[n].BeginAddress==0xCF5E5E0)native_primary=&native_records[n];
    check(native_primary!=nullptr,"source-pop unique canonical primary located");
    if(native_primary){
        const auto save_first=native_records[0];native_records[0]=*native_primary;
        check(!psm::items3446::Resolve(image).address,"duplicate pdata primary cannot qualify as unwind alias");
        native_records[0]=save_first;
    }
    check(ScanForItemGainAccess().address==base+0xDC6F5E0,"receipt key mutations restored");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("item3663 scanner failures=%d\n",failures);
    return failures?1:0;
}