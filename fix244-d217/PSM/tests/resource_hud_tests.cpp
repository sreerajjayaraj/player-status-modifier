#include "runtime/resource_hud_policy.h"
#include <cstdlib>
#include <iostream>

static void Check(bool ok, const char* label) {
    if (!ok) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
}

int main() {
    using namespace resource_hud;
    State hud;
    hud.RecordCost(0, 360000, 354000, 360000, 1000);
    Check(hud.Display(0, 360000, 360000, 1016) == 354000,
          "committed expense survives immediate authoritative refill");
    Check(hud.Display(0, 360000, 360000, 1016) == 354000,
          "repeated reads in a frame do not erase expense");
    Check(hud.Display(0, 340000, 360000, 1016) == 340000,
          "presentation never raises a lower native gauge");
    Check(hud.Display(0, 360000, 360000, 1121) == 360000,
          "short expense feedback cannot freeze recovery");
    Check(hud.Active(0, 1016), "recent expense keeps gauge visible");
    Check(!hud.Active(0, 1251), "native hide timer resumes after expense");
    hud.RecordCost(1, 130000, 122500, 130000, 2000);
    Check(hud.Display(1, 130000, 130000, 2016) == 122000,
          "Spirit uses native thousand-unit quantization");
    Check(hud.Display(1, 140000, 140000, 2016) == 140000,
          "max change rejects obsolete snapshot");
    hud.Reset();
    Check(!hud.Active(1, 2016), "reset invalidates previous actor activity");
    Check(hud.Display(1, 130000, 130000, 2016) == 130000,
          "reset invalidates previous actor expense");
    hud.RecordCost(0, 30000, 30000, 360000, 3000);
    hud.RecordCost(0, 30000, 31000, 360000, 3000);
    Check(!hud.Active(0, 3016), "no debit and healing do not create activity");
    hud.RecordCost(0, 30000, -1, 360000, 3000);
    Check(!hud.Active(0, 3016), "invalid native value fails closed");
    hud.SetContinuous(1);
    Check(hud.Active(0, 90000), "sustained native cost does not time out");
    Check(hud.Display(0, 360000, 360000, 90000) == 360000,
          "sustained net-positive regen does not fabricate a deficit");
    Check(!hud.Active(1, 90000), "physical resource roles stay separate");
    hud.SetContinuous(0);
    Check(!hud.Active(0, 90000), "native cost removal ends sustained activity");
    Check(IsNegativeContribution(-100, 0, 0), "native negative component");
    Check(IsNegativeContribution(-100, 500000, 0), "partly discounted native cost");
    Check(!IsNegativeContribution(-100, 1000000, 0), "fully waived cost");
    Check(!IsNegativeContribution(100000, 0, 0), "positive gear regen is not cost");
    Check(!IsNegativeContribution(0, 0, 0), "removed regen is not negative component");
    Check(!IsNegativeContribution(-100, -1, 0), "unvalidated modifier fails closed");
    std::cout << "resource HUD policy tests passed\n";
}
