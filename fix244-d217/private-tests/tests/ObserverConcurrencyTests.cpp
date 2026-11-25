#include <Windows.h>
#include "runtime/ship_trace.h"
#include "runtime/runtime_state.h"
#include <array>
#include <vector>
#include <string>
#include <cstdarg>
#include <cstdio>
#include <cstring>
std::mutex g_state_mutex;ActorResolveSnapshot g_player_resolve{};
static std::vector<std::string> logs;static uint64_t clockNow=1000;static unsigned checks=0,failures=0;
void Log(const char*f,...){char b[2048]{};va_list a;va_start(a,f);vsnprintf(b,sizeof b,f,a);va_end(a);logs.emplace_back(b);}
void Check(bool x,const char*n){++checks;if(!x){++failures;std::printf("FAIL %s\n",n);}}
uint64_t FakeClock(){return clockNow;}template<class T>uintptr_t Ptr(T&x){return reinterpret_cast<uintptr_t>(x.data());}template<size_t N,class T>void Put(std::array<unsigned char,N>&a,size_t o,T v){std::memcpy(a.data()+o,&v,sizeof v);}
struct FakeActor{std::array<unsigned char,0x400>a{},table{},vehicle{},ride{},dock{},status{},owner{},entries{},meta{};FakeActor(uint32_t uid,uint8_t category){Put(a,0x60,uid);Put(a,0x68,Ptr(table));Put(a,0x88,Ptr(meta));meta[1]=category;Put(table,0x48,Ptr(vehicle));Put(vehicle,0x18,Ptr(ride));Put(table,0x170,Ptr(dock));Put(table,0x20,Ptr(status));Put(status,0x18,Ptr(owner));Put(owner,0x58,Ptr(entries));Put(entries,0,uint16_t{0});Put(entries,8,int64_t{300000});Put(entries,0x18,int64_t{500000});}};
void Tick(){
#ifdef PSM_SHIP_OBSERVER_DIAGNOSTICS
 psm::shiptrace::Heartbeat();
#endif
}
void Player(uintptr_t a,uintptr_t m){
#ifdef PSM_SHIP_OBSERVER_DIAGNOSTICS
 psm::shiptrace::NoteAuthoritativePlayerCapture();psm::shiptrace::ObserveAuthoritativePlayer(a,m);
#else
 (void)a;(void)m;
#endif
}
size_t Count(const char*part){size_t n=0;for(const auto&s:logs)if(s.find(part)!=std::string::npos)++n;return n;}
bool Contains(const char*part){return Count(part)>0;}

#include <thread>
#include <atomic>
#include <immintrin.h>
#include "../../PSM/src/runtime/ship_trace.cpp"

int main(){using namespace psm::shiptrace;FakeActor player(10,1),ride(200,7);g_player_resolve.actor=Ptr(player.a);g_player_resolve.marker=Ptr(player.status);Put(player.ride,0x10,uint32_t{200});Put(player.dock,0x18,uint32_t{200});const auto saved=player;Enable(true,FakeClock);logs.clear();
 SetLastError(0x13572468);Arrive(Callback::Actor);Arrive(Callback::Unridden);Reject(Failure::NoIdentity);NoteAuthoritativePlayerCapture();Check(GetLastError()==0x13572468,"added mounted-origin and reason/capture counters preserve Win32 error");
 const auto originalCsr=_mm_getcsr(),seeded=(originalCsr|0x1f80u)&~0x3fu;SetLastError(0x12344321);_mm_setcsr(seeded);ObserveAuthoritativePlayer(Ptr(player.a),Ptr(player.status));Check(GetLastError()==0x12344321&&_mm_getcsr()==seeded,"raw callback preserves LastError/MXCSR");Check(logs.empty(),"authoritative native callback never formats or flushes added logs");
 void*guard=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);Check(guard!=nullptr,"private foreign-pointer guardpage allocated");DWORD old=0;Check(VirtualProtect(guard,4096,PAGE_READWRITE|PAGE_GUARD,&old)!=0,"guardpage enabled");clockNow+=300;SetLastError(0xABCDEF01);ObserveAuthoritativePlayer(reinterpret_cast<uintptr_t>(guard),Ptr(player.status));Check(GetLastError()==0xABCDEF01,"rejected raw callback preserves error status");MEMORY_BASIC_INFORMATION mbi{};Check(VirtualQuery(guard,&mbi,sizeof mbi)&&(mbi.Protect&PAGE_GUARD),"unbound foreign actor pointer is rejected before dereference");VirtualFree(guard,0,MEM_RELEASE);
 auto hold=[&](std::mutex&m,auto&&action){std::atomic<bool>ready{false},release{false};std::thread thread([&]{std::lock_guard lock(m);ready=true;while(!release.load())std::this_thread::yield();});while(!ready.load())std::this_thread::yield();action();release=true;thread.join();};
 hold(trace_mutex,[&]{Heartbeat();clockNow+=300;ObserveAuthoritativePlayer(Ptr(player.a),Ptr(player.status));});Check(Count("observer-")==0,"busy passive trace mutex emits no partial passive summary; independent proof worker may report");const auto after_worker=logs.size();Check(psm::shiptrace::failures[static_cast<unsigned>(Failure::RawBusy)].load()!=0,"raw publication contention is counted");
 hold(g_state_mutex,[&]{clockNow+=300;ObserveAuthoritativePlayer(Ptr(player.a),Ptr(player.status));});Check(psm::shiptrace::failures[static_cast<unsigned>(Failure::RawAuthority)].load()!=0,"current-player authority mutex contention fails closed without waiting");
 auto&counter=callbacks[static_cast<unsigned>(Callback::PlayerCapture)];counter.store(UINT32_MAX-1);for(unsigned i=0;i<100;++i)NoteAuthoritativePlayerCapture();Check(counter.load()==UINT32_MAX,"arrival counters saturate without wrapping");counter.store(0);std::vector<std::thread>threads;for(unsigned i=0;i<8;++i)threads.emplace_back([]{for(unsigned j=0;j<10000;++j)NoteAuthoritativePlayerCapture();});for(auto&t:threads)t.join();Check(counter.load()>0&&counter.load()<=80000,"contended approximate counters remain bounded and race-free");Check(logs.size()==after_worker,"parallel callback counters produce no callback logging");Check(std::memcmp(&player,&saved,sizeof player)==0,"coherent raw samples do not mutate native-shaped objects");for(auto&value:psm::shiptrace::failures)value.store(UINT32_MAX);for(auto&value:callbacks)value.store(UINT32_MAX);probe.raw.uid=UINT32_MAX;probe.raw.ride=UINT32_MAX;probe.raw.dock=UINT32_MAX;clockNow+=1000000;Heartbeat();Check(!logs.empty()&&logs.back().size()<1024,"worst-width diagnostic summary fits existing1024B logger payload");Enable(false);_mm_setcsr(originalCsr);std::printf("observer concurrency/status: %u checks %u failures; private authored source only\n",checks,::failures);return ::failures?1:0;}

