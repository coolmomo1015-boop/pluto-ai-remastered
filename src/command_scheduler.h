#pragma once
#include "commands.h"
#include <algorithm>
#include <cstdint>
#include <vector>
#include <utility>

// Higher base priority values are scheduled first. Ageing prevents starvation.
inline unsigned command_packet_priority(const Packet& packet) {
  if (packet.empty()) return 1;
  const auto opcode=packet[0];

  // Economy, construction, unit production, morphing and technology.
  switch(opcode) {
    case 0x0c: // Placebox / building placement
    case 0x18: // Cancel construction
    case 0x19: // Cancel morph
    case 0x1e: // Return cargo
    case 0x1f: // Train unit
    case 0x20: // Cancel train
    case 0x23: // Morph unit
    case 0x27: // Build fighter
    case 0x30: // Research
    case 0x31: // Cancel research
    case 0x32: // Upgrade
    case 0x33: // Cancel upgrade
    case 0x34: // Cancel addon
    case 0x35: // Zerg building morph
      return 4;
    case 0x61:
      // The legacy 0x15 "selected units action" packet contains opaque fields,
      // not a documented BWAPI UnitCommandType. Do not infer Gather/Repair from
      // arbitrary coordinate/order bytes.
      return 3;
    case 0x60: return 2; // Move/right-click; resource targets are classified below
    case 0x63: // Select units
    case 0x64: // Add selection
    case 0x65: return 1; // Remove selection
    default: return 3;
  }
}

inline bool is_resource_target_unit_type(uint16_t unit_type) {
  // BWAPI UnitTypes: Mineral Field 176-178, Vespene Geyser 188,
  // Terran Refinery 110, Zerg Extractor 149, Protoss Assimilator 157.
  return (unit_type>=176 && unit_type<=178) ||
    unit_type==188 || unit_type==110 || unit_type==149 || unit_type==157;
}

inline unsigned command_packet_priority(const Packet& packet,uint16_t target_unit_type) {
  if (!packet.empty() && packet[0]==0x60 &&
      is_resource_target_unit_type(target_unit_type)) return 4;
  return command_packet_priority(packet);
}

inline bool is_selection_packet(const Packet& packet) {
  return !packet.empty() &&
    (packet[0]==0x63 || packet[0]==0x64 || packet[0]==0x65);
}

// Keep a selection packet and the actions that use that selection together.
// Independent commands become their own schedulable turns, so a long mixed
// frame cannot make every command inherit the same priority and wait time.
inline std::vector<std::vector<Packet>> group_command_packets(const std::vector<Packet>& packets) {
  constexpr size_t kMaximumPacketsPerGroup=8;
  std::vector<std::vector<Packet>> groups;
  std::vector<Packet> selection_prefix;
  std::vector<Packet> selected_group;
  bool continuation_prefix=false;
  for (const auto& packet:packets) {
    if (is_selection_packet(packet)) {
      if (continuation_prefix) {
        selected_group.clear();
        selection_prefix.clear();
        continuation_prefix=false;
      } else if (selected_group.size()>selection_prefix.size()) {
        groups.push_back(std::move(selected_group));
        selected_group.clear();
        selection_prefix.clear();
      }
      selection_prefix.push_back(packet);
      selected_group.push_back(packet);
    } else if (!selected_group.empty()) {
      selected_group.push_back(packet);
      continuation_prefix=false;
      if (selected_group.size()>=kMaximumPacketsPerGroup) {
        groups.push_back(std::move(selected_group));
        selected_group=selection_prefix;
        continuation_prefix=true;
      }
    } else {
      groups.push_back({packet});
    }
  }
  if (!selected_group.empty() && !continuation_prefix)
    groups.push_back(std::move(selected_group));
  return groups;
}

template<class Classifier>
inline unsigned command_turn_priority(const std::vector<Packet>& packets,Classifier classifier) {
  unsigned priority=1;
  for (const auto& packet:packets)
    priority=std::max(priority,classifier(packet));
  // A selection-only turn is a precondition for commands that may follow in
  // the next observation, so do not leave it behind combat traffic.
  if(priority==1)return 3;
  return priority;
}

inline unsigned command_turn_priority(const std::vector<Packet>& packets) {
  return command_turn_priority(packets,[](const Packet& packet){
    return command_packet_priority(packet);
  });
}

inline unsigned command_turn_schedule_score(unsigned priority,uint64_t age_ms) {
  const unsigned ageing=static_cast<unsigned>(std::min<uint64_t>(4,age_ms/1000));
  return priority+ageing;
}

inline uint64_t command_turn_ttl_ms(unsigned priority) {
  if (priority>=4) return 12000;
  if (priority<=2) return 5000;
  return 8000;
}

inline bool command_turn_is_replaceable(const std::vector<Packet>& packets) {
  if (packets.empty()) return false;
  // Never collapse production/building/research commands. Only replace an
  // identical pending tactical/selection intent that has not begun dispatch.
  for (const auto& packet:packets) {
    if (packet.empty()) return false;
    const auto opcode=packet[0];
    if (opcode!=0x60 && opcode!=0x61 &&
        opcode!=0x63 && opcode!=0x64 && opcode!=0x65) return false;
    if (command_packet_priority(packet)>=4) return false;
  }
  return true;
}
