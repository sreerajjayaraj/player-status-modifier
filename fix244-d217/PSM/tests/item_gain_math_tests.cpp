#include "runtime/item_gain_math.h"
#include <array>
#include <bit>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

int main(int argc,char** argv){
 if(argc==2&&std::strcmp(argv[1],"--probe")==0){
  std::string qtoken,btoken;
  while(std::cin>>qtoken>>btoken){
   int64_t amount=0;uint64_t bits=0;
   if(std::from_chars(qtoken.data(),qtoken.data()+qtoken.size(),amount).ec!=std::errc{}||
      std::from_chars(btoken.data(),btoken.data()+btoken.size(),bits,16).ec!=std::errc{})return 2;
   const auto factor=std::bit_cast<double>(bits);int64_t target=0;
   const bool ok=psm::items::TryScaleDecimalQuantity(amount,factor,target);
   char text[64]{};const auto converted=std::to_chars(text,text+sizeof(text),factor,std::chars_format::general);
   if(converted.ec!=std::errc{})return 3;
   std::cout<<amount<<' '<<btoken<<' '<<std::string(text,converted.ptr)<<' '<<(ok?1:0)<<' '<<target<<'\n';
  }
  return 0;
 }
 const struct{int64_t amount;double factor;bool ok;int64_t expected;} cases[]={
  {3,0.5,true,1},{5,1.2,true,6},{7,2.5,true,17},{30,0.1,true,3},
  {100,0.29,true,29},{3,1.3333333333333333,true,3},
  {9007199254740993LL,1.5,true,13510798882111489LL},
  {9007199254740995LL,0.5,true,4503599627370497LL},
  {std::numeric_limits<int64_t>::max(),0.5,true,4611686018427387903LL},
  {std::numeric_limits<int64_t>::max(),1e-12,true,9223372},
  {std::numeric_limits<int64_t>::max(),2e-18,true,18},
  {1000000000,1e-6,true,1000},{1,1e6,true,1000000},
  {std::numeric_limits<int64_t>::max(),1.0000000000000002,false,std::numeric_limits<int64_t>::max()},
  {1,std::numeric_limits<double>::denorm_min(),false,1},
  {1,1e308,false,1},{1,0.1,false,1},{0,3,false,0},{-1,3,false,-1},
  {1,0,false,1},{1,-1,false,1},{1,std::numeric_limits<double>::infinity(),false,1},
  {1,std::numeric_limits<double>::quiet_NaN(),false,1}
 };
 int failures=0;
 for(const auto& c:cases){int64_t target=0;const auto ok=psm::items::TryScaleDecimalQuantity(c.amount,c.factor,target);
  const bool pass=ok==c.ok&&target==c.expected;if(!pass)++failures;
  std::printf("%s: exact decimal quantity=%lld factor=%.17g result=%lld ok=%d\n",pass?"PASS":"FAIL",c.amount,c.factor,target,ok?1:0);
 }
 std::printf("exact item quantity math checks=%zu failures=%d\n",std::size(cases),failures);return failures?1:0;
}
