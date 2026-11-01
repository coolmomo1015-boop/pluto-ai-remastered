#include "legacy_view.h"
#include "pluto_bindings.h"
#include "scr_profile_13515_x86.h"
#include "scr_layout.h"
#include "commands.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

namespace {
template<class T> void put(uintptr_t p,T value){std::memcpy(reinterpret_cast<void*>(p),&value,sizeof(value));}
thread_local LegacyView* current_view=nullptr;
}
LegacyView::LegacyView():memory(last-first){}
void LegacyView::reset() {
  std::fill(memory.begin(),memory.end(),uint8_t{0});slots={};sprites={};lookup.clear();
  raw_start=raw_length=0;flush_error.clear();sent=0;pending_turns.clear();
  command_rate_limiter.reset();
}
void LegacyView::prune_pending_turns(uint64_t now_ms,FILE* log) {
  for(auto it=pending_turns.begin();it!=pending_turns.end();) {
    if(it->next_packet==0 && now_ms>=it->expires_at_ms) {
      std::fprintf(log,"QUEUE EXPIRED frame=%d priority=%u age_ms=%llu\n",
        it->frame,it->priority,static_cast<unsigned long long>(now_ms-it->queued_at_ms));
      it=pending_turns.erase(it);
    } else ++it;
  }
}
void LegacyView::enqueue_turn(int frame,std::vector<Packet> packets,uint64_t now_ms,FILE* log) {
  if(packets.empty())return;
  const unsigned priority=command_turn_priority(packets);
  const uint64_t ttl=command_turn_ttl_ms(priority);

  if(command_turn_is_replaceable(packets)) {
    for(auto it=pending_turns.begin();it!=pending_turns.end();) {
      if(it->next_packet==0 && it->packets==packets) {
        std::fprintf(log,"QUEUE COALESCED frame=%d\n",it->frame);
        it=pending_turns.erase(it);
      } else ++it;
    }
  }

  if(pending_turns.size()>=kMaximumPendingTurns) {
    auto victim=pending_turns.end();
    for(auto it=pending_turns.begin();it!=pending_turns.end();++it) {
      if(it->next_packet!=0)continue;
      if(victim==pending_turns.end() || it->priority<victim->priority ||
         (it->priority==victim->priority && it->queued_at_ms<victim->queued_at_ms))
        victim=it;
    }
    if(victim==pending_turns.end() || victim->priority>priority) {
      std::fprintf(log,"QUEUE REJECTED frame=%d priority=%u\n",frame,priority);
      return;
    }
    std::fprintf(log,"QUEUE EVICTED frame=%d priority=%u\n",victim->frame,victim->priority);
    pending_turns.erase(victim);
  }

  pending_turns.push_back({frame,now_ms,now_ms+reaction_delay_ms,now_ms+ttl,priority,std::move(packets),0});
}
size_t LegacyView::next_pending_turn(uint64_t now_ms) const {
  if(pending_turns.empty())return pending_turns.size();
  // Once a turn starts, finish its unit-selection/action sequence atomically.
  if(pending_turns.front().next_packet!=0)return 0;

  size_t best=pending_turns.size();
  unsigned best_score=0;
  uint64_t best_age=0;
  for(size_t i=0;i<pending_turns.size();++i) {
    const auto& turn=pending_turns[i];
    if(now_ms<turn.not_before_ms)continue;
    const uint64_t age=now_ms>=turn.queued_at_ms?now_ms-turn.queued_at_ms:0;
    const unsigned ageing=static_cast<unsigned>(std::min<uint64_t>(4,age/1000));
    const unsigned score=turn.priority+ageing;
    if(best==pending_turns.size() || score>best_score ||
       (score==best_score && age>best_age)) {
      best=i;best_score=score;best_age=age;
    }
  }
  return best;
}
void LegacyView::begin_frame(FILE* log,int frame) {
  current_view=this;frame_log=log;frame_number=frame;sent=0;
  const auto now=GetTickCount64();
  prune_pending_turns(now,log);
  if(pending_turns.empty())return;
  const auto index=next_pending_turn(now);
  if(index>=pending_turns.size())return;
  if(!command_rate_limiter.allow(now))return;

  if(index!=0)std::swap(pending_turns[0],pending_turns[index]);
  auto& turn=pending_turns.front();
  const auto send=reinterpret_cast<void (__cdecl*)(const uint8_t*,size_t)>(scr::send_command());
  const auto& packet=turn.packets[turn.next_packet++];
  send(packet.data(),packet.size());++sent;
  std::fprintf(log,"SEND frame=%d queued_frame=%d priority=%u queue_age_ms=%llu packet=",
    frame,turn.frame,turn.priority,static_cast<unsigned long long>(now-turn.queued_at_ms));
  for(auto b:packet)std::fprintf(log,"%02x",b);
  std::fprintf(log," submitted=true apm_window=%u apm_limit=%u\n",
    command_rate_limiter.used_in_window(),command_rate_limiter.limit());
  if(turn.next_packet>=turn.packets.size())pending_turns.erase(pending_turns.begin());
}
void __cdecl LegacyView::flush_turn() noexcept {
  if(!current_view)return;
  try{current_view->drain(current_view->frame_log,current_view->frame_number);}
  catch(const std::exception& e){
    current_view->flush_error=e.what();
    put<uint32_t>(current_view->address(0x654aa0),0);
  }
}
uint32_t LegacyView::address(uint32_t old) const {
  if(old<first || old>=last)throw std::runtime_error("Out of range legacy address");
  return reinterpret_cast<uint32_t>(memory.data())+old-first;
}
uint32_t LegacyView::unit(uint32_t raw) const {
  auto found=lookup.find(raw);
  return found==lookup.end()?0:address(unit_base+found->second*336);
}
uint16_t LegacyView::handle_for_raw(uint32_t raw) const {
  auto found=lookup.find(raw);if(found==lookup.end())return 0;
  auto index=found->second;return static_cast<uint16_t>((index+1)|(slots[index].generation<<11));
}
uint32_t LegacyView::scr_handle(uint16_t id) const {
  unsigned index=id&0x7ff;if(!index || index>slots.size())return 0;--index;
  auto& slot=slots[index];if(!slot.raw || slot.generation!=(id>>11))return 0;
  return (slot.raw-raw_start)/336+1 | (uint32_t(slot.source_generation)<<(raw_length>1700?13:11));
}
void LegacyView::sprite(uint32_t dest,uint32_t raw) {
  std::memcpy(reinterpret_cast<void*>(dest),reinterpret_cast<void*>(raw),20);
  put<uint32_t>(dest,0);put<uint32_t>(dest+4,0);
  put<uint16_t>(dest+20,static_cast<uint16_t>(scr::read<uint32_t>(raw+20)));
  put<uint16_t>(dest+22,static_cast<uint16_t>(scr::read<uint32_t>(raw+24)));
  std::memcpy(reinterpret_cast<void*>(dest+24),reinterpret_cast<void*>(raw+28),12);
}
void LegacyView::update() {
  raw_start=scr::units();raw_length=scr::read<uint32_t>(scr::addr(0x10436f4));
  if(!raw_start || !raw_length || raw_length>8192)throw std::runtime_error("Invalid SCR unit vector");
  std::unordered_set<uint32_t> present;
  for(unsigned i=0;i<raw_length;++i) {
    auto raw=raw_start+i*336;
    if(scr::read<uint32_t>(raw+12) && scr::read<uint16_t>(raw+100)<228)present.insert(raw);
  }
  if(present.size()>slots.size())throw std::runtime_error("Pluto's legacy unit view exceeded 1700 simultaneous units");
  for(unsigned i=0;i<slots.size();++i)if(slots[i].raw && !present.count(slots[i].raw)) {
    lookup.erase(slots[i].raw);slots[i].raw=0;
    std::memset(reinterpret_cast<void*>(address(unit_base+i*336)),0,336);
  }
  for(auto raw:present) {
    const auto gen=scr::read<uint8_t>(raw+165);auto found=lookup.find(raw);
    if(found==lookup.end()) {
      auto empty=std::find_if(slots.begin(),slots.end(),[](auto& s){return s.raw==0;});
      auto index=static_cast<unsigned>(empty-slots.begin());
      empty->raw=raw;empty->source_generation=gen;empty->generation=(empty->generation+1)&31;lookup.emplace(raw,index);
    }else if(slots[found->second].source_generation!=gen) {
      auto& slot=slots[found->second];slot.source_generation=gen;slot.generation=(slot.generation+1)&31;
    }
  }
  std::memcpy(reinterpret_cast<void*>(address(0x57f0f0)),reinterpret_cast<void*>(scr::game()),layout::Game::size);
  std::memcpy(reinterpret_cast<void*>(address(0x57eee0)),reinterpret_cast<void*>(scr::players()),12*36);
  put<uint32_t>(address(0x512688),scr::local_player_id());
  put<uint32_t>(address(0x59688c),scr::is_multiplayer()?1u:0u);put<uint16_t>(address(0x596904),2);
  put<uint32_t>(address(0x628430),unit(scr::first_active_unit()));
  put<uint32_t>(address(0x6283ec),unit(scr::first_hidden_unit()));
  put<uint32_t>(address(0x6283f4),unit(scr::read<uint32_t>(scr::addr(0x10436b8))));
  put<uint32_t>(address(0x6d1260),scr::map_tile_flags());
  for(unsigned i=0;i<slots.size();++i)if(slots[i].raw) {
    const auto raw=slots[i].raw,dest=address(unit_base+i*336);
    std::memcpy(reinterpret_cast<void*>(dest),reinterpret_cast<void*>(raw),336);
    for(unsigned offset:{0,4,20,92,104,108,112,124,128,236,240,244,252,284})put<uint32_t>(dest+offset,unit(scr::read<uint32_t>(raw+offset)));
    // Unit-specific unions contain both pointers and scalar values. Convert a
    // field only when it points exactly at a currently allocated unit slot.
    for(unsigned offset=192;offset<=216;offset+=4) {
      auto value=scr::read<uint32_t>(raw+offset);if(lookup.count(value))put<uint32_t>(dest+offset,unit(value));
    }
    auto raw_sprite=scr::read<uint32_t>(raw+12);sprite(reinterpret_cast<uint32_t>(sprites[i].data()),raw_sprite);
    put<uint32_t>(dest+12,reinterpret_cast<uint32_t>(sprites[i].data()));put<uint8_t>(dest+165,slots[i].generation);
    const auto high=raw_length>1700?scr::read<uint16_t>(raw+306):0;
    for(unsigned n=0;n<8;++n) {
      const uint32_t id=scr::read<uint16_t>(raw+176+n*2)|(((high>>(n*2))&3)<<16);
      const uint32_t index=id&(raw_length>1700?0x1fff:0x7ff);
      uint16_t legacy=0;
      if(index && index<=raw_length) {
        auto loaded=raw_start+(index-1)*336;
        if(scr::read<uint8_t>(loaded+165)==(id>>(raw_length>1700?13:11)))legacy=handle_for_raw(loaded);
      }
      put<uint16_t>(dest+176+n*2,legacy);
    }
  }
  std::vector<uint32_t> raw_bullets;
  std::unordered_set<uint32_t> visited_bullets;
  for(auto bullet=scr::read<uint32_t>(scr::addr(0x1034a78));bullet;bullet=scr::read<uint32_t>(bullet+4)) {
    if(raw_bullets.size()>=8192 || !visited_bullets.insert(bullet).second)throw std::runtime_error("Invalid SCR bullet list");
    raw_bullets.push_back(bullet);
  }
  bullets.resize(raw_bullets.size());bullet_sprites.resize(raw_bullets.size());
  const auto bullet_address=[this](size_t index){return reinterpret_cast<uint32_t>(bullets[index].data());};
  put<uint32_t>(address(0x64dec4),bullets.empty()?0:bullet_address(0));
  for(size_t count=0;count<raw_bullets.size();++count) {
    const auto bullet=raw_bullets[count],dest=bullet_address(count);
    std::memcpy(reinterpret_cast<void*>(dest),reinterpret_cast<void*>(bullet),112);
    put<uint32_t>(dest,count?bullet_address(count-1):0);
    put<uint32_t>(dest+4,count+1<bullets.size()?bullet_address(count+1):0);
    for(unsigned offset:{20,92,100,104})put<uint32_t>(dest+offset,unit(scr::read<uint32_t>(bullet+offset)));
    auto raw_sprite=scr::read<uint32_t>(bullet+12);
    if(raw_sprite){
      const auto sprite_address=reinterpret_cast<uint32_t>(bullet_sprites[count].data());
      sprite(sprite_address,raw_sprite);put<uint32_t>(dest+12,sprite_address);
    }
  }
}
void LegacyView::bind(HMODULE pluto) {
  const auto base=reinterpret_cast<uint32_t>(pluto);
  for(auto& binding:pluto_bindings)if(scr::read<uint32_t>(base+binding.rva)!=binding.expected)throw std::runtime_error("Pluto binding validation failed; unsupported binary");
  for(auto& binding:pluto_bindings) {
    const int32_t signed_address=static_cast<int32_t>(binding.expected);
    uint32_t value;
    if(binding.expected==0x485a40)value=reinterpret_cast<uint32_t>(&LegacyView::flush_turn);
    else if(signed_address<0)value=0u-address(static_cast<uint32_t>(-signed_address));
    else value=address(binding.expected);
    DWORD old;
    auto at=reinterpret_cast<void*>(base+binding.rva);
    if(!VirtualProtect(at,4,PAGE_EXECUTE_READWRITE,&old))throw std::runtime_error("Cannot bind Pluto binary");
    put<uint32_t>(base+binding.rva,value);
    DWORD unused;VirtualProtect(at,4,old,&unused);
  }
  FlushInstructionCache(GetCurrentProcess(),nullptr,0);
}
unsigned LegacyView::drain(FILE* log,int frame) {
  if(!flush_error.empty())throw std::runtime_error(flush_error);
  const auto size=scr::read<uint32_t>(address(0x654aa0));
  if(size>512)throw std::runtime_error("Legacy command queue overflow");
  if(size){
    auto packets=translate_commands(reinterpret_cast<const uint8_t*>(address(0x654880)),size,[this](uint16_t id){return scr_handle(id);});
    // Keep each observation's packet sequence intact, while prioritizing
    // economic work, expiring stale intent and bounding the pending backlog.
    enqueue_turn(frame,std::move(packets),GetTickCount64(),log);
  }
  put<uint32_t>(address(0x654aa0),0);
  return sent;
}
