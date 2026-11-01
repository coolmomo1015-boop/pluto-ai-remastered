#include "command_scheduler.h"
#include <cassert>

int main() {
  assert(command_packet_priority(Packet{0x0c,0,0,0,0,0,0,0})==4); // Build
  assert(command_packet_priority(Packet{0x1f,0,0})==4); // Train
  assert(command_packet_priority(Packet{0x1e,0})==4); // Return cargo
  assert(command_packet_priority(Packet{0x27})==4); // Carrier/Reaver fighter
  assert(command_packet_priority(Packet{0x20,0,0})==4); // Cancel train
  assert(command_packet_priority(Packet{0x63,0})==1); // Selection
  assert(command_turn_priority({Packet{0x63,0}})==3); // Standalone selection
  assert(command_packet_priority(Packet{0x60,0})==2); // Right-click/tactical

  Packet action_packet(13,0);
  action_packet[0]=0x61;
  action_packet[11]=15; // Opaque legacy action-packet field, not BWAPI command type.
  assert(command_packet_priority(action_packet)==3);
  Packet attack{0x60,0,0,0,0,0,0,0,0,0,0,0,0};
  Packet right_click{0x60,0,0,0,0,1,0,0,0,0,0,0,0};

  assert(is_resource_target_unit_type(176));
  assert(is_resource_target_unit_type(177));
  assert(is_resource_target_unit_type(178));
  assert(is_resource_target_unit_type(188));
  assert(is_resource_target_unit_type(110));
  assert(is_resource_target_unit_type(149));
  assert(is_resource_target_unit_type(157));
  assert(!is_resource_target_unit_type(0)); // Empty / no target is not a resource.
  assert(command_packet_priority(right_click)==2);
  assert(command_packet_priority(right_click,176)==4); // Mineral field -> prioritized resource action.
  assert(command_packet_priority(right_click,188)==4); // Gas geyser -> prioritized resource action.
  assert(command_packet_priority(right_click,110)==4); // Refinery -> prioritized resource action.
  assert(command_packet_priority(action_packet)==3);
  assert(command_turn_priority({Packet{0x63,0},right_click},
    [](const Packet& packet){return command_packet_priority(packet,176);})==4);
  const auto groups=group_command_packets({
    Packet{0x63,0},attack,action_packet,Packet{0x64,0},Packet{0x1f,0,0},right_click
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
  switched_selection.push_back(right_click);
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
