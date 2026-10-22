// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Art for companions, read from the player's own disc at run time: window frames, icons and
// portraits. Nothing from the game ships with a companion; it names a file on the disc instead:
//
//   disc:window/level.tpl#3                 image 3 of a TPL texture file
//   disc:Face/IKE.cms#13                    .cms/.cmp files are LZ10-compressed
//   disc:zmap/bmap01/map.cmp>texpack.tpl#0  a member of a "pack" archive
//   disc:Face/OSCAR.cms#-1                  the last image (counts differ between files)
//
// The image is decoded once with Dolphin's GameCube texture decoders and cached as a PNG.

#pragma once

#include <map>
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
  s32 index = 0;       // Image within the TPL; negative counts from the end (-1 is the last).
};

struct DecodedImage
{
  u32 width = 0;
  u32 height = 0;
  std::vector<u8> rgba;  // width * height * 4 bytes, R G B A.
};

// Parses "disc:<file>[><member>][#<index>]"; "#-1" is the last image. Returns nullopt for anything
// else or unsafe paths.
std::optional<DiscImageRef> ParseDiscImageRef(std::string_view source);

// Nintendo LZ10 (".cmp", ".cms"). Returns nullopt if the data is not valid LZ10.
std::optional<std::vector<u8>> DecompressLZ10(std::span<const u8> data);

// Decompresses LZ10 data and returns anything else unchanged.
std::vector<u8> MaybeDecompress(std::vector<u8> data);

// A member of an Intelligent Systems "pack" archive.
std::optional<std::vector<u8>> ExtractPackMember(std::span<const u8> pack, std::string_view name);

// Decodes image `index` of a GameCube TPL texture file; a negative index counts from the end.
std::optional<DecodedImage> DecodeTPL(std::span<const u8> tpl, s32 index, std::string* error);

// Decodes an Intelligent Systems message table (Fire Emblem's ".m" files) into key -> text, both
// UTF-8. Keys are Shift-JIS; text is Shift-JIS when `shift_jis` is set (Japanese discs), otherwise
// Windows-1252.
std::optional<std::map<std::string, std::string, std::less<>>>
ParseMessageTable(std::span<const u8> data, bool shift_jis);

// Reads a file from the game partition of a disc image. Thread-safe; opened discs are cached.
std::optional<std::vector<u8>> ReadDiscFile(const std::string& disc_path, std::string_view file);

// Closes the discs ReadDiscFile keeps open (when emulation stops).
void CloseDiscFiles();

// Returns the path of a PNG for `source`, decoding it from the disc into `cache_directory` on first
// use. `error` explains a failure.
std::optional<std::string> MaterializeDiscImage(const std::string& disc_path,
                                                const std::string& cache_directory,
                                                std::string_view source, std::string* error);
struct Manifest;

using TextTables = std::map<std::string, std::map<std::string, std::string>, std::less<>>;

// Reads disc-backed tables ("tables": {"msg": "disc:Mess/common.m"}, given here as name -> file)
// from the disc. Problems are appended to `errors`; tables that fail are left out. Touches no
// emulator state, so it can run on a worker thread.
TextTables ReadDiscTables(const std::map<std::string, std::string, std::less<>>& sources,
                          const std::string& disc_path, std::string_view game_id,
                          std::vector<std::string>* errors);

// Merges `tables` into the manifest's tables.
void InstallTextTables(Manifest& manifest, TextTables tables);

// ReadDiscTables + InstallTextTables, for tools.
void LoadDiscTables(Manifest& manifest, const std::string& disc_path, std::string_view game_id,
                    std::vector<std::string>* errors);
}  // namespace Duo
