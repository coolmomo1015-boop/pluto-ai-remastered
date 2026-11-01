#pragma once
#include "commands.h"
#include <algorithm>
#include <cstdint>
#include <vector>

// Higher base priority values are scheduled first. Ageing prevents starvation.
inline unsigned command_packet_priority(const Packet& packet) {
  if (packet.empty()) return 1;
  const auto opcode=packet[0];

  // Economy, construction, unit production, morphing and technology.
  switch(opcode) {
    case 0x0c: // Placebox / building placement
    case 0x1e: // Return cargo
    case 0x1f: // Train unit
    case 0x23: // Morph unit
    case 0x30: // Research
    case 0x32: // Upgrade
    case 0x35: // Zerg building morph
      return 4;
    case 0x61: // Action packet (translated from legacy 0x15)
      if (packet.size() >= 12) {
        const auto action=packet[11];
        // BWAPI UnitCommandTypes: build, addon, train, morph, research,
        // upgrade, rally, gather, return cargo and repair.
        if ((action>=2 && action<=9) || (action>=15 && action<=17)) return 4;
        if (action<=14) return 2; // attack, move, patrol, hold and tactical orders
      }
      return 3;
    case 0x60: return 2; // Right-click / movement intent
    case 0x63: // Select units
    case 0x64: // Add selection
    case 0x65: return 1; // Remove selection
    default: return 3;
  }
}

inline bool is_selection_packet(const Packet& packet) {
  return !packet.empty() &&
    (packet[0]==0x63 || packet[0]==0x64 || packet[0]==0x65);
}

// Keep a selection packet and the actions that use that selection together.
// Independent commands become their own schedulable turns, so a long mixed
// frame cannot make every command inherit the same priority and wait time.
inline std::vector<std::vector<Packet>> group_command_packets(const std::vector<Packet>& packets) {
  std::vector<std::vector<Packet>> groups;
  std::vector<Packet> selected_group;
  for (const auto& packet:packets) {
    if (is_selection_packet(packet)) {
      if (!selected_group.empty()) {
        groups.push_back(std::move(selected_group));
        selected_group.clear();
      }
      selected_group.push_back(packet);
    } else if (!selected_group.empty()) {
      selected_group.push_back(packet);
    } else {
      groups.push_back({packet});
    }
  }
  if (!selected_group.empty())groups.push_back(std::move(selected_group));
  return groups;
}

inline unsigned command_turn_priority(const std::vector<Packet>& packets) {
  unsigned priority=1;
  for (const auto& packet:packets)
    priority=std::max(priority,command_packet_priority(packet));
  // A selection-only turn is a precondition for commands that may follow in
  // the next observation, so do not leave it behind combat traffic.
  if(priority==1)return 3;
  return priority;
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
