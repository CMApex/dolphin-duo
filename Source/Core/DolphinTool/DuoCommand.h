// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

namespace DolphinTool
{
// dolphin-tool duo validate|preview ... : tools for Dolphin Duo companion authors.
int DuoCommand(const std::vector<std::string>& args);
}  // namespace DolphinTool
