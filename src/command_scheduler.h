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

inline unsigned command_turn_priority(const std::vector<Packet>& packets) {
  unsigned priority=1;
  for (const auto& packet:packets)
    priority=std::max(priority,command_packet_priority(packet));
  return priority;
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
