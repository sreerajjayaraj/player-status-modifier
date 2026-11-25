#include "runtime/item_gain_math.h"
#include <bit>
#include <iostream>
#include <string>
int main(){
 long long amount;unsigned long long bits;
 while(std::cin>>amount>>bits){
  const auto factor=std::bit_cast<double>(static_cast<uint64_t>(bits));
  char canonical[128]{};const auto text=std::to_chars(canonical,canonical+sizeof(canonical),factor,std::chars_format::general);
  int64_t result=amount;const auto ok=psm::items::TryScaleDecimalQuantity(amount,factor,result);
  std::cout<<ok<<' '<<result<<' '<<std::string(canonical,text.ptr)<<'\n';
 }
}
