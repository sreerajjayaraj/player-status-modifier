#include "config.h"
#include <filesystem>
#include <fstream>
#include <cstdio>
unsigned checks=0,failures=0;void Check(bool ok,const char*n){++checks;if(!ok){++failures;std::printf("FAIL %s\n",n);}}
bool Enabled(const ModConfig&c){
#ifdef PSM_SHIP_HEALING_CONFIG
 return c.ship_healing.enabled;
#else
 (void)c;return false;
#endif
}
double Mult(const ModConfig&c){
#ifdef PSM_SHIP_HEALING_CONFIG
 return c.ship_healing.multiplier;
#else
 (void)c;return 1;
#endif
}
int main(){const auto p=std::filesystem::current_path()/"ship-config-test.ini";ModConfig c{};Check(Enabled(c)&&Mult(c)==100,"missing ship section defaults to independent requested ship boost100");{std::ofstream f(p);f<<"[Health]\nEnabled=1\nHealMultiplier=3\nConsumptionMultiplier=0.1\n[Items]\nGainMultiplier=3\n[ShipHealing]\nEnabled=1\nMultiplier=12.5\n";}const auto bytes=[&]{std::ifstream f(p);return std::string((std::istreambuf_iterator<char>(f)),{});}();Check(ReadConfigSnapshot(p.wstring(),&c),"real config reader succeeds");Check(Enabled(c)&&Mult(c)==12.5,"ship section reads independent decimal multiplier");Check(c.health.enabled&&c.health.heal_multiplier==3&&c.health.consumption_multiplier==0.1,"ship settings preserve independent player health math");{std::ifstream f(p);Check(std::string((std::istreambuf_iterator<char>(f)),{})==bytes,"reader never appends or rewrites supplied INI");}{std::ofstream f(p);f<<"[Health]\nEnabled=0\nHealMultiplier=99\n[ShipHealing]\nEnabled=1\nMultiplier=7\n";}ReadConfigSnapshot(p.wstring(),&c);Check(Enabled(c)&&Mult(c)==7&&!c.health.enabled&&c.health.heal_multiplier==1,"ship healing remains independent when player health disabled");{std::ofstream f(p);f<<"[ShipHealing]\nEnabled=0\nMultiplier=9\n";}ReadConfigSnapshot(p.wstring(),&c);Check(!Enabled(c)&&Mult(c)==9,"ship-only switch disables policy without resetting independent multiplier");{std::ofstream f(p);f<<"[ShipHealing]\nEnabled=1\nMultiplier=nan\n";}ReadConfigSnapshot(p.wstring(),&c);Check(Enabled(c)&&Mult(c)==100,"invalid nonfinite ship multiplier retains safe default");{std::ofstream f(p);f<<"[ShipHealing]\nMultiplier=-5\n";}ReadConfigSnapshot(p.wstring(),&c);Check(Mult(c)==100,"negative ship multiplier rejected by real parser");{std::ofstream f(p);f<<"[Health]\nEnabled=1\nHealMultiplier=25\n";}ReadConfigSnapshot(p.wstring(),&c);Check(Enabled(c)&&Mult(c)==100&&c.health.heal_multiplier==25,"missing ship keys do not inherit player heal25");ModConfig only{};only.health.enabled=false;only.stamina.enabled=false;only.spirit.enabled=false;only.mount.enabled=false;only.damage.incoming.enabled=false;only.damage.outgoing.enabled=false;only.oxygen_unlimited=false;
 Check(Enabled(only)&&ShouldInstallLegacyStatWriteHook(only)&&ShouldInstallSharedStatHooks(only),"ship-only startup requires generic writer and shared native observation installation");Check(!ShouldInstallDamageHook(only)&&!ShouldInstallStaminaHook(only)&&!ShouldInstallSpiritHook(only),"ship-only startup does not require player health damage stamina or Spirit options");
#ifdef PSM_SHIP_HEALING_CONFIG
 only.ship_healing.enabled=false;
#endif
 Check(!ShouldInstallLegacyStatWriteHook(only)&&!ShouldInstallSharedStatHooks(only),"ship-only disabled state permits existing config watcher to detect hook dependency change");
 std::printf("independent ship config: %u checks %u failures\n",checks,failures);return failures?1:0;}
