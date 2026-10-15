// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The narrow view of emulated memory a companion gets. Reads return host-order values (the
// big-endian conversion of the GameCube/Wii happens behind this interface) and never raise
// user-visible alerts: an unmapped address is simply a failed read.

#pragma once

#include <optional>

#include "Common/CommonTypes.h"

namespace Duo
{
class MemorySource
{
public:
  virtual ~MemorySource() = default;

  // `size` is 1, 2, 4 or 8 bytes. Addresses are effective addresses (0x80000000-style), the same
  // addresses Dolphin's memory view and Dolphin Memory Engine show.
  virtual std::optional<u64> Read(u32 address, u32 size) = 0;
  virtual bool Write(u32 address, u32 size, u64 value) = 0;

  // Whether `address` currently maps to RAM. Used to stop pointer chains at junk pointers.
  virtual bool IsRAM(u32 address) = 0;
};
}  // namespace Duo
