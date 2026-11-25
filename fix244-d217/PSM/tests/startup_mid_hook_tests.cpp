#include "hooks/startup_mid_hook.h"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <expected>
#include <vector>
namespace {
unsigned checks=0, failures=0, callback_calls=0;
void Check(bool ok,const char* name) {++checks;if(!ok){++failures;std::printf("FAIL: %s\n",name);}}
void Callback(SafetyHookContext& ctx) {++callback_calls;ctx.rax=43;}
using Result=std::expected<SafetyHookMid,safetyhook::MidHook::Error>;
Result TransientError(){return std::unexpected{safetyhook::MidHook::Error::bad_allocation(safetyhook::Allocator::Error::BAD_VIRTUAL_ALLOC)};}
struct SyntheticCode {
    unsigned char* p=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    SyntheticCode(){if(p){std::memset(p,0x90,4096);p[0]=0xB8;p[1]=42;p[2]=p[3]=p[4]=0;p[16]=0xC3;FlushInstructionCache(GetCurrentProcess(),p,4096);}}
    ~SyntheticCode(){if(p)VirtualFree(p,0,MEM_RELEASE);}
    uintptr_t site(){return reinterpret_cast<uintptr_t>(p+5);}
    int run(){return reinterpret_cast<int(*)()>(p)();}
    bool untouched(){return p[5]==0x90&&p[6]==0x90&&p[7]==0x90&&p[8]==0x90&&p[9]==0x90;}
};
void Scenario(unsigned failures_before_success) {
    SyntheticCode code;Check(code.p!=nullptr,"private synthetic function allocated");if(!code.p)return;
    Check(code.run()==42,"synthetic function baseline returns42");
    unsigned resolves=0,creates=0,reports=0,waits=0;std::vector<unsigned> report_attempts;
    auto hook=psm::startup::CreateValidatedMid([&]{++resolves;return code.site();},[&](uintptr_t address)->Result{
        ++creates;if(creates<=failures_before_success)return TransientError();
        return SafetyHookMid::create(reinterpret_cast<void*>(address),Callback);
    },[&](unsigned attempt,uintptr_t address,const safetyhook::MidHook::Error& error){
        ++reports;report_attempts.push_back(attempt);Check(address==code.site(),"error retains current validated site");
        Check(error.type==safetyhook::MidHook::Error::BAD_ALLOCATION,"typed allocation error reaches report");
    },[&](unsigned ms){++waits;Check(ms==250,"retry delay is bounded250ms");});
    const auto expected_attempts=failures_before_success<3?failures_before_success+1:3;
    Check(resolves==expected_attempts,"native site is freshly resolved for every attempt");
    Check(creates==expected_attempts,"creation has bounded attempts");
    Check(reports==(failures_before_success<3?failures_before_success:3),"every failure is reported");
    Check(waits==(failures_before_success<3?failures_before_success:2),"no wait after final outcome");
    for(unsigned i=0;i<report_attempts.size();++i)Check(report_attempts[i]==i+1,"failure attempt numbering is exact");
    if(failures_before_success<3){
        Check(bool(hook),"transient failure can recover a real hook");
        if(hook){const auto before=callback_calls;Check(code.run()==43,"installed private hook applies callback");Check(callback_calls==before+1,"callback executes exactly once");hook.reset();}
    }else Check(!hook,"exhausted attempts stay inert");
    Check(code.untouched(),"no stale patch remains after failure or reset");
    Check(code.run()==42,"native synthetic result restored");
}
}
int main(){
    for(unsigned n: {0u,1u,2u,9u})Scenario(n);
    {
        unsigned resolves=0,creates=0,waits=0,reports=0;
        auto hook=psm::startup::CreateValidatedMid([&]()->uintptr_t{++resolves;return 0;},[&](uintptr_t)->Result{++creates;return TransientError();},[&](unsigned,uintptr_t,const auto&){++reports;},[&](unsigned){++waits;});
        Check(!hook&&resolves==1&&creates==0&&waits==0&&reports==0,"missing contract never creates or retries a hook");
    }
    {
        SyntheticCode code;unsigned resolves=0,creates=0,waits=0;
        auto hook=psm::startup::CreateValidatedMid([&]()->uintptr_t{++resolves;return resolves==1?code.site():0;},[&](uintptr_t)->Result{++creates;return TransientError();},[&](unsigned,uintptr_t,const auto&){},[&](unsigned){++waits;});
        Check(!hook&&resolves==2&&creates==1&&waits==1,"lost native contract aborts without reusing stale site");
        Check(code.untouched(),"contract loss never patches private code");
    }
    {
        SyntheticCode first,second;unsigned resolves=0,creates=0;
        auto hook=psm::startup::CreateValidatedMid([&]{++resolves;return resolves==1?first.site():second.site();},[&](uintptr_t address)->Result{++creates;if(creates==1)return TransientError();return SafetyHookMid::create(reinterpret_cast<void*>(address),Callback);},[&](unsigned,uintptr_t,const auto&){},[&](unsigned){});
        Check(bool(hook)&&resolves==2,"retry uses the newly validated site");Check(first.untouched(),"stale first site remains untouched");
        if(hook){Check(second.run()==43,"new validated private site owns callback");hook.reset();}
    }
    {
        const auto stub=psm::startup::Describe(safetyhook::MidHook::Error::bad_allocation(safetyhook::Allocator::Error::BAD_VIRTUAL_ALLOC));
        Check(!std::strcmp(stub.category,"stub-allocation")&&stub.allocator_code==0&&stub.inline_code==-1&&stub.instruction==0,"stub allocation diagnostics do not interpret inactive union");
        const auto tramp=psm::startup::Describe(safetyhook::MidHook::Error::bad_inline_hook(safetyhook::InlineHook::Error::bad_allocation(safetyhook::Allocator::Error::NO_MEMORY_IN_RANGE)));
        Check(!std::strcmp(tramp.category,"trampoline-allocation")&&tramp.allocator_code==1&&tramp.inline_code==0&&tramp.instruction==0,"trampoline range error keeps active union fields");
        const char* names[]={"trampoline-allocation","instruction-decode","short-jump","relative-out-of-range","unsupported-instruction","memory-protection","insufficient-space"};
        for(unsigned i=1;i<7;++i){
            safetyhook::InlineHook::Error error{};error.type=static_cast<decltype(error.type)>(i);error.ip=reinterpret_cast<unsigned char*>(uintptr_t{0x123456});
            const auto d=psm::startup::Describe(safetyhook::MidHook::Error::bad_inline_hook(error));
            Check(!std::strcmp(d.category,names[i])&&d.inline_code==static_cast<int>(i)&&d.allocator_code==-1&&d.instruction==0x123456,"each inline error is named without losing instruction");
        }
    }
    std::printf("startup-mid-hook: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
