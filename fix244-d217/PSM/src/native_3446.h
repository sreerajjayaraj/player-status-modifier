#pragma once
#include "native_3446_profiles.h"
#include <Windows.h>
#include <array>
#include <cstring>
#include <algorithm>

namespace psm::v3446 {
// A build profile is accepted only after unique exact code discovery and
// complete .pdata fragment/canonical CHAININFO checks. No RVA lookup fallback.
class Image {
public:
 const unsigned char* data=nullptr; std::size_t size=0;
 IMAGE_NT_HEADERS64 nt{}; IMAGE_DATA_DIRECTORY exception{};
 std::array<IMAGE_SECTION_HEADER,96> sections{};
 template<class T> bool Read(std::size_t at,T& value) const {
  if(at>size || sizeof(T)>size-at)return false;
  std::memcpy(&value,data+at,sizeof(T));return true;
 }
 bool Range(std::size_t at,std::size_t n)const{return at<=size&&n<=size-at;}
 bool Exec(std::size_t at,std::size_t n=1)const {
  if(!Range(at,n))return false;
  for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i){const auto&s=sections[i];
   const auto extent=(std::max)(s.Misc.VirtualSize,s.SizeOfRawData);
   if((s.Characteristics&IMAGE_SCN_MEM_EXECUTE)&&at>=s.VirtualAddress&&at-s.VirtualAddress<=extent&&n<=extent-(at-s.VirtualAddress))return true;
  }return false;
 }
 bool Init(const void* p,std::size_t length){
  data=static_cast<const unsigned char*>(p);size=length;IMAGE_DOS_HEADER dos{};
  if(!p||!Read(0,dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<=0||!Read(dos.e_lfanew,nt)||nt.Signature!=IMAGE_NT_SIGNATURE||nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||nt.OptionalHeader.SizeOfImage>size||!nt.FileHeader.NumberOfSections||nt.FileHeader.NumberOfSections>sections.size())return false;
  size=nt.OptionalHeader.SizeOfImage;
  const std::size_t table=dos.e_lfanew+24+nt.FileHeader.SizeOfOptionalHeader;
  if(!Range(table,nt.FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER)))return false;
  for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i)if(!Read(table+i*sizeof(IMAGE_SECTION_HEADER),sections[i]))return false;
  exception=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
  return exception.Size&&exception.Size%sizeof(RUNTIME_FUNCTION)==0&&Range(exception.VirtualAddress,exception.Size);
 }
 bool UniqueRecord(std::size_t at,RUNTIME_FUNCTION& result)const {
  unsigned count=0;
  for(std::size_t i=0;i<exception.Size;i+=sizeof(RUNTIME_FUNCTION)){RUNTIME_FUNCTION r{};if(!Read(exception.VirtualAddress+i,r))return false;
   if(at>=r.BeginAddress&&at<r.EndAddress){if(++count!=1||!Exec(r.BeginAddress,r.EndAddress-r.BeginAddress))return false;result=r;}
  }return count==1;
 }
 // Some current CHAININFO records refer to a second byte-identical
 // terminal unwind descriptor. Boundaries must still match the unique .pdata
 // owner, and only complete no-handler/no-chain metadata can be equivalent.
 bool EquivalentLeafUnwind(DWORD a,DWORD b)const {
  unsigned char x[4]{},y[4]{};
  if((a&3)||(b&3)||!Range(a,4)||!Range(b,4))return false;
  std::memcpy(x,data+a,4);std::memcpy(y,data+b,4);
  if((x[0]!=1&&x[0]!=2)||std::memcmp(x,y,4))return false;
  const std::size_t length=4+((x[2]+1u)&~1u)*2;
  return Range(a,length)&&Range(b,length)&&!std::memcmp(data+a,data+b,length);
 }
 bool Canonical(RUNTIME_FUNCTION record,RUNTIME_FUNCTION& result)const {
  std::array<DWORD,8> seen{};
  for(unsigned depth=0;depth<seen.size();++depth){
   if(record.EndAddress<=record.BeginAddress||!Exec(record.BeginAddress,record.EndAddress-record.BeginAddress)||(record.UnwindData&3))return false;
   for(unsigned i=0;i<depth;++i)if(seen[i]==record.UnwindData)return false;
   seen[depth]=record.UnwindData;
   unsigned char header[4]{};
   if(!Range(record.UnwindData,4))return false;std::memcpy(header,data+record.UnwindData,4);
   const unsigned version=header[0]&7, flags=header[0]>>3;
   if((version!=1&&version!=2)||(flags&~7))return false;
   const std::size_t trailer=record.UnwindData+4+((header[2]+1u)&~1u)*2;
   if(!Range(record.UnwindData,trailer-record.UnwindData))return false;
   if(!(flags&4)){result=record;return true;}
   if(flags&3)return false;
   RUNTIME_FUNCTION chain{},actual{};
   if(!Read(trailer,chain)||!UniqueRecord(chain.BeginAddress,actual)||
      chain.BeginAddress!=actual.BeginAddress||chain.EndAddress!=actual.EndAddress||
      (chain.UnwindData!=actual.UnwindData&&!EquivalentLeafUnwind(chain.UnwindData,actual.UnwindData)))return false;
   record=actual;
  }return false;
 }
 bool Fragments(std::size_t start,const Profile& p)const {
  std::size_t found=0;
  for(std::size_t i=0;i<exception.Size;i+=sizeof(RUNTIME_FUNCTION)){RUNTIME_FUNCTION r{};if(!Read(exception.VirtualAddress+i,r))return false;
   if(r.BeginAddress>=start+p.size||r.EndAddress<=start)continue;
   if(found>=p.count)return false;
   const auto&expected=p.fragments[found++];RUNTIME_FUNCTION canonical{};
   if(r.BeginAddress!=start+expected.begin||r.EndAddress!=start+expected.end||!Canonical(r,canonical)||canonical.BeginAddress!=start+expected.root)return false;
  }return found==p.count;
 }
 std::size_t Find(const Profile&p,bool boundaries=true)const {
  std::size_t match=0;unsigned count=0;
  for(unsigned i=0;i<nt.FileHeader.NumberOfSections;++i){const auto&s=sections[i];const std::size_t length=(std::max)(s.Misc.VirtualSize,s.SizeOfRawData);
   if(!(s.Characteristics&IMAGE_SCN_MEM_EXECUTE)||length<p.size||!Range(s.VirtualAddress,length))continue;
   std::size_t cursor=s.VirtualAddress,last=cursor+length-p.size;
   while(cursor<=last){const auto*hit=static_cast<const unsigned char*>(std::memchr(data+cursor,p.code[0],last-cursor+1));if(!hit)break;cursor=hit-data;
    if(!std::memcmp(hit,p.code,(std::min)(p.size,std::size_t{16}))&&!std::memcmp(hit,p.code,p.size)){if(++count>1)return 0;match=cursor;}++cursor;
   }
  }return count==1&&(!boundaries||Fragments(match,p))?match:0;
 }
 std::size_t Branch(std::size_t at,unsigned char opcode=0xE8)const {
  std::int32_t delta=0;if(!Exec(at,5)||data[at]!=opcode||!Read(at+1,delta))return 0;
  const auto target=std::int64_t(at)+5+delta;return target>0&&Exec(std::size_t(target))?std::size_t(target):0;
 }
 bool RipDword(std::size_t at,unsigned displacement_offset,unsigned instruction_size,std::uint32_t& value)const {
  std::int32_t delta=0;if(!Exec(at,instruction_size)||!Read(at+displacement_offset,delta))return false;
  const auto target=std::int64_t(at)+instruction_size+delta;return target>0&&Read(std::size_t(target),value);
 }
 std::size_t Follow(std::size_t at)const {
  std::array<std::size_t,4>seen{};
  for(unsigned i=0;i<seen.size();++i){if(!Exec(at,5))return 0;for(unsigned j=0;j<i;++j)if(at==seen[j])return 0;seen[i]=at;if(data[at]!=0xE9)return at;at=Branch(at,0xE9);}return 0;
 }
};
inline std::size_t ModuleSize(const void*p){
 if(!p)return 0;const auto*d=static_cast<const IMAGE_DOS_HEADER*>(p);if(d->e_magic!=IMAGE_DOS_SIGNATURE||d->e_lfanew<=0||d->e_lfanew>0x100000)return 0;
 const auto*n=reinterpret_cast<const IMAGE_NT_HEADERS64*>(static_cast<const unsigned char*>(p)+d->e_lfanew);
 return n->Signature==IMAGE_NT_SIGNATURE?n->OptionalHeader.SizeOfImage:0;
}
// Select the register/layout family using stable slices outside its
// hook spans. Each individual user still proves its own complete body/edges.
inline bool HasCurrentResourceLayout(const Image& image) {
 constexpr std::size_t slot = 0x194CD30;
 constexpr std::size_t cost = 0xB80A43F;
 constexpr std::size_t slot_prefix = 0x24, cost_prefix = 0x1B;
 return (image.Exec(slot,slot_prefix) && !std::memcmp(image.data+slot,kSlot.code,slot_prefix)) ||
        (image.Exec(cost,cost_prefix) && !std::memcmp(image.data+cost,kCost.code,cost_prefix));
}
template<size_t N>
inline bool ExactUnwind(const Image& image, std::size_t site, const unsigned char (&expected)[N]) {
 RUNTIME_FUNCTION record{};
 return image.UniqueRecord(site,record) && image.Range(record.UnwindData,N) &&
        !std::memcmp(image.data+record.UnwindData,expected,N);
}
inline constexpr unsigned char kWriterUnwind3663[] =
 {0x01,0x15,0x06,0x00,0x15,0x72,0x11,0xE0,0x0F,0x70,0x0E,0x60,0x0D,0x50,0x0C,0x30};
inline constexpr unsigned char kReaderUnwind3663[] =
 {0x01,0x15,0x08,0x00,0x15,0x74,0x09,0x00,0x15,0x64,0x08,0x00,0x15,0x34,0x07,0x00,0x15,0x32,0x11,0xE0};
inline constexpr unsigned char kCostUnwind3663[] =
 {0x01,0x0C,0x06,0x00,0x0C,0x34,0x08,0x00,0x0C,0x32,0x08,0x70,0x07,0x60,0x06,0x50};
inline constexpr unsigned char kCaptureUnwind3663[] =
 {0x11,0x0A,0x04,0x00,0x0A,0x34,0x07,0x00,0x0A,0x32,0x06,0x70,0xF8,0x74,0xC2,0x04};
inline bool WriterFields(const Image&i,std::size_t w){
 // RDI is the entry and RBX is the clamped value at +43. The native
 // selector notification path uses direct zero tests on this build.
 constexpr unsigned char first[]={0x4C,0x8D,0x71,0x18},clamp[]={0x48,0x8D,0x77,0x18};
 constexpr unsigned char threshold[]={0x48,0x83,0x38,0x00};
 constexpr unsigned char terminal[]={0x41,0xC6,0x07,0x01};
 return w && i.Exec(w,0x134) && ExactUnwind(i,w,kWriterUnwind3663) &&
    !std::memcmp(i.data+w+0x15,first,sizeof(first)) &&
    !std::memcmp(i.data+w+0x3F,clamp,sizeof(clamp)) &&
    !std::memcmp(i.data+w+0x8F,threshold,sizeof(threshold)) &&
    !std::memcmp(i.data+w+0x12E,terminal,sizeof(terminal));
}
inline bool ResourceGroup(const Image&i,std::size_t rate,std::size_t cost,std::size_t reader){
 const auto c=i.Find(kCost),w=i.Find(kWriter),m=i.Find(kMetadata);
 std::int32_t delta=0;
 double divisor=0;
 return reader && c && w && m && WriterFields(i,w) &&
        ExactUnwind(i,reader,kReaderUnwind3663) && ExactUnwind(i,c,kCostUnwind3663) &&
        rate==reader+0x9B && cost==c+0x3B &&
        i.Branch(c+0x36)==reader && i.Branch(reader+0x21)==m &&
        i.Branch(c+0x95)==w && i.Branch(c+0x9D)==m && i.Branch(c+0xB6)==m &&
        i.Branch(w+0x8A)==reader && i.Branch(w+0xCC)==reader &&
        i.Read(reader+0x5E,delta) && i.Read(static_cast<std::size_t>(
            static_cast<std::int64_t>(reader+0x62)+delta),divisor) && divisor==1000.0;
}
}
