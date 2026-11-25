#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
namespace psm {
// The enclosing state/proof mutex supplies one writer. Every payload word is
// atomic: an unsuccessful sequence read is not an ordinary-memory data race.
template<std::size_t N> class AtomicWords {
 std::atomic<uint64_t> sequence_{0};
 std::array<std::atomic<uint64_t>,N> words_{};
public:
 using Words=std::array<uint64_t,N>;
 void BeginWrite(){sequence_.fetch_add(1,std::memory_order_acq_rel);}
 void EndWrite(const Words&words){for(std::size_t i=0;i<N;++i)words_[i].store(words[i],std::memory_order_relaxed);sequence_.fetch_add(1,std::memory_order_release);}
 void Store(const Words&words){BeginWrite();EndWrite(words);}
 bool Load(Words*out,uint64_t*version=nullptr)const{
  if(!out)return false;
  for(unsigned attempt=0;attempt<2;++attempt){
   const auto before=sequence_.load(std::memory_order_acquire);if(before&1)return false;
   Words copy{};for(std::size_t i=0;i<N;++i)copy[i]=words_[i].load(std::memory_order_relaxed);
   std::atomic_thread_fence(std::memory_order_acquire);
   if(before==sequence_.load(std::memory_order_acquire)){*out=copy;if(version)*version=before;return true;}
  }
  return false;
 }
 // Single-word hint only. Callers must prove the full state before acting.
 template<std::size_t I> uint64_t LoadHint()const{
  static_assert(I<N);
  return words_[I].load(std::memory_order_relaxed);
 }
 Words LoadForWriter()const{Words copy{};for(std::size_t i=0;i<N;++i)copy[i]=words_[i].load(std::memory_order_relaxed);return copy;}
 uint64_t Version()const{return sequence_.load(std::memory_order_acquire);}
};
}
