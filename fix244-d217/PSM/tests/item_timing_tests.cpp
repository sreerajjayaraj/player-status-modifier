#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "runtime/item_timing.h"
#include <atomic>
#include <thread>
#include <vector>
#include <cstdio>
#include <limits>
#include <cstdarg>
namespace it=psm::item_timing;
std::atomic<uint64_t> now{100},logs{0};unsigned checks=0,failures=0;
uint64_t ResetClock()noexcept;
uint64_t Clock()noexcept{SetLastError(0xBAD);return now.fetch_add(10);}
uint64_t ResetClock()noexcept{it::Enable(false,Clock,1000000);return now.fetch_add(10);}
void Log(const char*,...){logs.fetch_add(1);SetLastError(0xBAE);}
void Check(bool b,const char*n){++checks;if(!b){++failures;std::printf("FAIL %s\n",n);}}
it::Span Plan(uintptr_t object=0x10000){auto p=it::Begin(it::Kind::Plan,object,0x123);it::Mark(p,it::Phase::Prepared);it::Mark(p,it::Phase::NativeBegin);it::Mark(p,it::Phase::NativeEnd);return p;}
int main(){
 it::Sample out[256]{};SetLastError(0x98765432);it::Enable(false,Clock,1000000);auto disabled=Plan();it::End(disabled,true,true,false);Check(it::Drain(out,256)==0,"disabled captures nothing");Check(GetLastError()==0x98765432,"disabled helpers preserve LastError");
 it::Enable(true,Clock,1000000);SetLastError(0x98765432);auto p=Plan();auto id=p.sample.id;it::End(p,true,true,false,0xABC,1);
 Check(it::Drain(out,1)==1&&out[0].id==id&&out[0].prepared&&out[0].completed&&out[0].result_raw==0xABC,"actual plan phases and result queued");
 Check(out[0].ticks[0]<out[0].ticks[1]&&out[0].ticks[1]<out[0].ticks[2]&&out[0].ticks[2]<out[0].ticks[3]&&out[0].ticks[3]<out[0].ticks[4],"monotonic five-phase plan packet");
 Check(GetLastError()==0x98765432,"clock and all queue helpers preserve LastError");Check(logs.load()==0,"native callback packet path never logs");
 auto push=it::Begin(it::Kind::Push,0x10000,0x456);Check(push.sample.parent==0,"push never infers Plan identity after Plan returned");auto pushId=push.sample.id;it::Mark(push,it::Phase::NativeBegin);it::Mark(push,it::Phase::NativeEnd);it::End(push,false,true,false,0xDEF);
 auto receipt=it::Begin(it::Kind::Receipt,0x20000,0x789,pushId);it::Mark(receipt,it::Phase::NativeBegin);it::Mark(receipt,it::Phase::NativeEnd);it::End(receipt,false,true,false,0xFED);
 Check(it::Drain(out,256)==2&&out[0].parent==0&&out[1].parent==pushId,"receipt binds explicitly provided existing push TLS generation");
 auto consumed=it::Begin(it::Kind::Push,0x10000);Check(!consumed.sample.parent,"plan association is consumed once");it::End(consumed,false,true,false);
 auto wrongThread=Plan(0x30000);it::End(wrongThread,true,true,false);std::thread t([&]{auto x=it::Begin(it::Kind::Push,0x30000);Check(!x.sample.parent,"different thread never inherits plan association");it::End(x,false,true,false);});t.join();
 auto invalid=Plan(0x30000);it::End(invalid,false,true,false);auto noStale=it::Begin(it::Kind::Push,0x30000);Check(!noStale.sample.parent,"new rejected plan clears stale completed association");it::End(noStale,false,true,false);
 it::Enable(true,Clock,1000000);auto old=Plan();it::Enable(true,Clock,1000000);it::End(old,true,true,false);Check(!it::Drain(out,256),"lifecycle reset rejects in-flight old epoch");
 auto abnormal=it::Begin(it::Kind::Receipt,0x40000,0,123);it::Mark(abnormal,it::Phase::NativeBegin);it::End(abnormal,false,false,true);Check(it::Drain(out,1)==1&&out[0].abnormal&&!out[0].completed,"exception packet retains unfinished native phase");
 it::Enable(true,Clock,1000000);for(unsigned i=0;i<200;++i){auto rejected=Plan();it::End(rejected,false,true,false);auto unrelated=it::Begin(it::Kind::Push,0x20000,0,0,false);it::End(unrelated,false,true,false);}auto firstQualified=Plan();it::End(firstQualified,true,true,false);Check(it::Drain(out,1)==1,"loading rejected/background requests do not exhaust qualified Plan budget");
 it::Enable(true,Clock,1000000);for(unsigned k=0;k<3;++k)for(unsigned i=0;i<80;++i){auto x=it::Begin(static_cast<it::Kind>(k),0x50000+i,0,k==2?123:0);it::Mark(x,it::Phase::NativeBegin);it::Mark(x,it::Phase::NativeEnd);it::End(x,true,true,false);}
 auto statistics=it::GetStatistics();Check(statistics.queued<=128&&statistics.dropped>0,"fixed queue saturation and per-kind budgets remain bounded");Check(logs.load()==0,"saturated callback path still never logs");
 SetLastError(0x98765432);auto before=logs.load();it::Heartbeat();Check(logs.load()>before&&logs.load()-before<=18,"worker reports at most16 packets plus one bounded drop summary and ready marker");Check(GetLastError()==0x98765432,"worker reporting preserves its caller LastError");
 it::Enable(true,Clock,1000000);it::TestLock();auto busy=it::Begin(it::Kind::Plan,0x50000);it::End(busy,true,true,false);it::TestUnlock();Check(it::GetStatistics().contended>0,"native path uses try-lock and records contention without waiting");
 it::Enable(true,Clock,1000000);auto backwards=Plan();now.store(1);it::End(backwards,true,true,false);Check(!it::Drain(out,256)&&it::GetStatistics().clock_rejected>0,"backwards clocks never publish duration");
 it::Enable(true,Clock,1000000);it::TestSequence(std::numeric_limits<uint64_t>::max());auto wrapped=Plan();it::End(wrapped,true,true,false);Check(!it::Drain(out,256)&&it::GetStatistics().sequence_exhausted,"sequence wrap fails closed until explicit lifecycle reset");
 it::Enable(true,Clock,1000000);std::vector<std::thread> producers;for(unsigned k=0;k<4;++k)producers.emplace_back([=]{for(unsigned i=0;i<1000;++i){auto x=Plan(0x60000+k);it::End(x,true,true,false);}});for(auto& thread:producers)thread.join();auto count=it::Drain(out,256);Check(count<=64&&it::GetStatistics().queued==0,"concurrent producers retain bounded exact packet drain");
 it::Enable(true,Clock,1000000);const auto logStart=logs.load();for(unsigned i=0;i<10000;++i){auto x=Plan();it::End(x,true,true,false);it::Heartbeat();}Check(logs.load()-logStart<=69,"10000 postcap cycles produce at most64 packets, one ready marker and four drop summaries");
 it::Enable(false);const auto stoppedLogs=logs.load();it::Heartbeat();Check(logs.load()==stoppedLogs,"disabled epoch never emits false ready marker");
 it::Enable(true,ResetClock,1000000);auto raced=it::Begin(it::Kind::Plan,0x70000);it::End(raced,true,true,false);Check(!raced.active&&!it::Drain(out,256),"Begin detects lifecycle reset during clock capture and rejects mixed epoch");
 it::Enable(false);Check(!it::Begin(it::Kind::Plan,0x10000).active,"teardown disables new observations");
 std::printf("item-timing: %u checks, %u failures; authored clock/packets only\n",checks,failures);return failures?1:0;
}
