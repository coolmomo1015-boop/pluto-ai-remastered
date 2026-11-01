#include "command_scheduler.h"
#include <cassert>

int main() {
  assert(command_packet_priority(Packet{0x0c,0,0,0,0,0,0,0})==4); // Build
  assert(command_packet_priority(Packet{0x1f,0,0})==4); // Train
  assert(command_packet_priority(Packet{0x1e,0})==4); // Return cargo
  assert(command_packet_priority(Packet{0x63,0})==1); // Selection
  assert(command_turn_priority({Packet{0x63,0}})==3); // Standalone selection
  assert(command_packet_priority(Packet{0x60,0})==2); // Right-click/tactical

  Packet gather(13,0);
  gather[0]=0x61;
  gather[11]=15; // BWAPI Gather
  assert(command_packet_priority(gather)==4);

  Packet repair(13,0);
  repair[0]=0x61;
  repair[11]=17; // BWAPI Repair
  assert(command_packet_priority(repair)==4);

  Packet attack(13,0);
  attack[0]=0x61;
  attack[11]=1; // BWAPI Attack_Unit
  assert(command_packet_priority(attack)==2);

  assert(command_turn_priority({Packet{0x63,0},gather})==4);
  const auto groups=group_command_packets({
    Packet{0x63,0},attack,gather,Packet{0x64,0},Packet{0x1f,0,0},Packet{0x60,0}
  });
  assert(groups.size()==2);
  assert(groups[0].size()==3); // Selection plus its following orders.
  assert(groups[1].size()==3); // Next selection and all following orders.

  std::vector<Packet> long_selection_group{Packet{0x63,0}};
  for(unsigned i=0;i<15;++i)long_selection_group.push_back(attack);
  const auto bounded_groups=group_command_packets(long_selection_group);
  assert(bounded_groups.size()==3);
  for(const auto& group:bounded_groups) {
    assert(group.size()<=8);
    assert(is_selection_packet(group.front())); // Selection reapplied for each chunk.
  }

  std::vector<Packet> switched_selection{Packet{0x63,0}};
  for(unsigned i=0;i<7;++i)switched_selection.push_back(attack);
  switched_selection.push_back(Packet{0x64,0});
  switched_selection.push_back(gather);
  const auto switched_groups=group_command_packets(switched_selection);
  assert(switched_groups.size()==2);
  assert(switched_groups[0].size()==8);
  assert(switched_groups[1].size()==2);
  assert(switched_groups[1][0][0]==0x64);
  assert(command_turn_ttl_ms(4)==12000);
  assert(command_turn_ttl_ms(2)==5000);
  assert(command_turn_schedule_score(2,0)==2);
  assert(command_turn_schedule_score(2,3000)>command_turn_schedule_score(4,0));
  assert(command_turn_is_replaceable({attack}));
  assert(!command_turn_is_replaceable({Packet{0x1f,0,0}}));
  assert(!command_turn_is_replaceable({Packet{0x63,0},Packet{0x0c,0,0}}));
  return 0;
}
