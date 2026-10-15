// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Art for companions, read from the player's own disc at run time: window frames, icons and
// portraits. Nothing from the game ships with a companion; it names a file on the disc instead:
//
//   disc:window/level.tpl#3                 image 3 of a TPL texture file
//   disc:Face/IKE.cms#13                    .cms/.cmp files are LZ10-compressed
//   disc:zmap/bmap01/map.cmp>texpack.tpl#0  a member of a "pack" archive
//
// The image is decoded once with Dolphin's GameCube texture decoders and cached as a PNG.

#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Common/CommonTypes.h"

namespace Duo
{
struct DiscImageRef
{
  std::string file;    // Path on the disc, e.g. "window/level.tpl".
  std::string member;  // File inside a "pack" archive, or empty.
  u32 index = 0;       // Image within the TPL.
};

struct DecodedImage
{
  u32 width = 0;
  u32 height = 0;
  std::vector<u8> rgba;  // width * height * 4 bytes, R G B A.
};

// Parses "disc:<file>[><member>][#<index>]". Returns nullopt for anything else or unsafe paths.
std::optional<DiscImageRef> ParseDiscImageRef(std::string_view source);

// Nintendo LZ10 (".cmp", ".cms"). Returns nullopt if the data is not valid LZ10.
std::optional<std::vector<u8>> DecompressLZ10(std::span<const u8> data);

// Decompresses LZ10 data and returns anything else unchanged.
std::vector<u8> MaybeDecompress(std::vector<u8> data);

// A member of an Intelligent Systems "pack" archive.
std::optional<std::vector<u8>> ExtractPackMember(std::span<const u8> pack, std::string_view name);

// Decodes image `index` of a GameCube TPL texture file.
std::optional<DecodedImage> DecodeTPL(std::span<const u8> tpl, u32 index, std::string* error);

// Reads a file from the game partition of a disc image. Thread-safe; opened discs are cached.
std::optional<std::vector<u8>> ReadDiscFile(const std::string& disc_path, std::string_view file);

// Returns the path of a PNG for `source`, decoding it from the disc into `cache_directory` on first
// use. `error` explains a failure.
std::optional<std::string> MaterializeDiscImage(const std::string& disc_path,
                                                const std::string& cache_directory,
                                                std::string_view source, std::string* error);
}  // namespace Duo
