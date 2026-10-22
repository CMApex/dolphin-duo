// Copyright 2026 Dolphin Duo Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/Duo/DiscAssets.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <map>
#include <memory>
#include <mutex>

#include <fmt/format.h>

#include "Common/FileUtil.h"
#include "Common/Hash.h"
#include "Common/Image.h"
#include "Common/StringUtil.h"
#include "Common/Swap.h"
#include "Core/Duo/Manifest.h"
#include "DiscIO/DiscExtractor.h"
#include "DiscIO/Filesystem.h"
#include "DiscIO/Volume.h"
#include "VideoCommon/TextureDecoder.h"

namespace Duo
{
namespace
{
constexpr u32 TPL_MAGIC = 0x0020AF30;
constexpr size_t MAX_FILE_SIZE = 64 * 1024 * 1024;
constexpr u32 MAX_DIMENSION = 4096;

u32 Read32(std::span<const u8> data, size_t offset)
{
  return offset + 4 <= data.size() ? Common::swap32(data.data() + offset) : 0;
}

u16 Read16(std::span<const u8> data, size_t offset)
{
  return offset + 2 <= data.size() ? Common::swap16(data.data() + offset) : 0;
}

bool IsSafeDiscPath(std::string_view path)
{
  if (path.empty() || path.size() > 200 || path.starts_with('/') || path.find("..") != path.npos)
    return false;
  return std::ranges::all_of(path, [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '/' || c == '_' || c == '-' ||
           c == '.';
  });
}

int BlockWidth(TextureFormat format)
{
  return TexDecoder_GetBlockWidthInTexels(format);
}

int BlockHeight(TextureFormat format)
{
  return TexDecoder_GetBlockHeightInTexels(format);
}
}  // namespace

std::optional<DiscImageRef> ParseDiscImageRef(std::string_view source)
{
  if (!source.starts_with("disc:"))
    return std::nullopt;
  source.remove_prefix(5);

  DiscImageRef ref;
  if (const size_t hash = source.rfind('#'); hash != source.npos)
  {
    const std::string_view number = source.substr(hash + 1);
    const auto [end, ec] = std::from_chars(number.data(), number.data() + number.size(), ref.index);
    if (ec != std::errc() || end != number.data() + number.size() || ref.index > 255 ||
        ref.index < -256)
      return std::nullopt;
    source = source.substr(0, hash);
  }
  if (const size_t arrow = source.find('>'); arrow != source.npos)
  {
    ref.member = source.substr(arrow + 1);
    source = source.substr(0, arrow);
    // Members are only looked up by name inside the archive; some have folder-like names.
    if (!IsSafeDiscPath(ref.member))
      return std::nullopt;
  }
  ref.file = source;
  if (!IsSafeDiscPath(ref.file))
    return std::nullopt;
  return ref;
}

std::optional<std::vector<u8>> DecompressLZ10(std::span<const u8> data)
{
  if (data.size() < 4 || data[0] != 0x10)
    return std::nullopt;
  const size_t size = data[1] | (data[2] << 8) | (data[3] << 16);
  if (size > MAX_FILE_SIZE)
    return std::nullopt;

  std::vector<u8> out;
  out.reserve(size);
  size_t in = 4;
  while (out.size() < size)
  {
    if (in >= data.size())
      return std::nullopt;
    const u8 flags = data[in++];
    for (int bit = 7; bit >= 0 && out.size() < size; --bit)
    {
      if (flags & (1 << bit))
      {
        if (in + 1 >= data.size())
          return std::nullopt;
        const size_t length = (data[in] >> 4) + 3;
        const size_t distance = (((data[in] & 0xF) << 8) | data[in + 1]) + 1;
        in += 2;
        if (distance > out.size())
          return std::nullopt;
        for (size_t i = 0; i < length && out.size() < size; ++i)
          out.push_back(out[out.size() - distance]);
      }
      else
      {
        if (in >= data.size())
          return std::nullopt;
        out.push_back(data[in++]);
      }
    }
  }
  return out;
}

std::vector<u8> MaybeDecompress(std::vector<u8> data)
{
  if (!data.empty() && data[0] == 0x10)
  {
    if (std::optional<std::vector<u8>> decompressed = DecompressLZ10(data))
      return std::move(*decompressed);
  }
  return data;
}

std::optional<std::vector<u8>> ExtractPackMember(std::span<const u8> pack, std::string_view name)
{
  // "pack", u16 count, u16 0, then per member: u32 0, u32 name offset, u32 data offset, u32 size.
  if (pack.size() < 8 || std::string_view(reinterpret_cast<const char*>(pack.data()), 4) != "pack")
    return std::nullopt;
  const u16 count = Read16(pack, 4);
  for (u32 i = 0; i < count; ++i)
  {
    const size_t entry = 8 + i * 16;
    if (entry + 16 > pack.size())
      return std::nullopt;
    const u32 name_offset = Read32(pack, entry + 4);
    const u32 data_offset = Read32(pack, entry + 8);
    const u32 size = Read32(pack, entry + 12);
    if (name_offset >= pack.size())
      continue;
    const auto name_end = std::find(pack.begin() + name_offset, pack.end(), u8{0});
    const std::string_view member(reinterpret_cast<const char*>(pack.data()) + name_offset,
                                  static_cast<size_t>(name_end - (pack.begin() + name_offset)));
    if (member != name)
      continue;
    if (static_cast<u64>(data_offset) + size > pack.size())
      return std::nullopt;
    return std::vector<u8>(pack.begin() + data_offset, pack.begin() + data_offset + size);
  }
  return std::nullopt;
}

std::optional<DecodedImage> DecodeTPL(std::span<const u8> tpl, s32 requested, std::string* error)
{
  const auto fail = [&](std::string message) -> std::optional<DecodedImage> {
    if (error)
      *error = std::move(message);
    return std::nullopt;
  };
  if (Read32(tpl, 0) != TPL_MAGIC)
    return fail("not a TPL texture");
  const u32 count = Read32(tpl, 4);
  const u32 table = Read32(tpl, 8);
  const s64 wanted = requested < 0 ? static_cast<s64>(count) + requested : requested;
  if (wanted < 0 || wanted >= count)
    return fail(fmt::format("the texture has {} images, not #{}", count, requested));
  const u32 index = static_cast<u32>(wanted);

  const u32 image_header = Read32(tpl, table + index * 8);
  const u32 palette_header = Read32(tpl, table + index * 8 + 4);
  if (image_header == 0 || image_header + 0x24 > tpl.size())
    return fail("broken TPL image header");

  const u32 height = Read16(tpl, image_header);
  const u32 width = Read16(tpl, image_header + 2);
  const u32 format_value = Read32(tpl, image_header + 4);
  const u32 data_offset = Read32(tpl, image_header + 8);
  if (width == 0 || height == 0 || width > MAX_DIMENSION || height > MAX_DIMENSION)
    return fail("bad TPL image size");
  if (format_value > 0xE || format_value == 7 || format_value == 0xB || format_value == 0xC ||
      format_value == 0xD)
  {
    return fail(fmt::format("unsupported TPL format {}", format_value));
  }
  const TextureFormat format = static_cast<TextureFormat>(format_value);

  // The decoder works on whole blocks, so decode into a padded image and crop.
  const u32 block_w = static_cast<u32>(BlockWidth(format));
  const u32 block_h = static_cast<u32>(BlockHeight(format));
  const u32 padded_w = (width + block_w - 1) / block_w * block_w;
  const u32 padded_h = (height + block_h - 1) / block_h * block_h;
  const u32 data_size = static_cast<u32>(TexDecoder_GetTextureSizeInBytes(
      static_cast<int>(padded_w), static_cast<int>(padded_h), format));
  if (static_cast<u64>(data_offset) + data_size > tpl.size())
    return fail("TPL image data is truncated");

  std::vector<u8> palette;
  const u8* tlut = nullptr;
  TLUTFormat tlut_format = TLUTFormat::IA8;
  if (format == TextureFormat::C4 || format == TextureFormat::C8 ||
      format == TextureFormat::C14X2)
  {
    if (palette_header == 0 || palette_header + 12 > tpl.size())
      return fail("palette texture without a palette");
    const u16 entries = Read16(tpl, palette_header);
    const u32 palette_format = Read32(tpl, palette_header + 4);
    const u32 palette_offset = Read32(tpl, palette_header + 8);
    if (palette_format > 2 || static_cast<u64>(palette_offset) + entries * 2u > tpl.size())
      return fail("broken TPL palette");
    // The decoder indexes the palette by texel value whatever the entry count says, so give it a
    // zero-filled palette of the format's full size: C4 16, C8 256 and C14X2 16384 entries.
    const size_t full_entries = format == TextureFormat::C4 ? 16 :
                                format == TextureFormat::C8 ? 256 :
                                                              16384;
    palette.assign(full_entries * 2, 0);
    std::copy_n(tpl.begin() + palette_offset, std::min<size_t>(entries, full_entries) * 2,
                palette.begin());
    tlut = palette.data();
    tlut_format = static_cast<TLUTFormat>(palette_format);
  }

  std::vector<u8> padded(static_cast<size_t>(padded_w) * padded_h * 4);
  TexDecoder_Decode(padded.data(), tpl.data() + data_offset, static_cast<int>(padded_w),
                    static_cast<int>(padded_h), format, tlut, tlut_format);

  DecodedImage image;
  image.width = width;
  image.height = height;
  image.rgba.resize(static_cast<size_t>(width) * height * 4);
  for (u32 y = 0; y < height; ++y)
  {
    std::copy_n(padded.begin() + static_cast<size_t>(y) * padded_w * 4, width * 4,
                image.rgba.begin() + static_cast<size_t>(y) * width * 4);
  }
  return image;
}

std::optional<std::map<std::string, std::string, std::less<>>>
ParseMessageTable(std::span<const u8> data, bool shift_jis)
{
  // u32 file size, u32 text size, u32 0, u32 entry count, 16 bytes padding, the text, then
  // (u32 text offset, u32 key offset) per entry, then the key strings.
  if (data.size() < 0x20)
    return std::nullopt;
  const u32 text_size = Read32(data, 4);
  const u32 count = Read32(data, 12);
  const u64 entries = 0x20 + u64{text_size};
  const u64 keys = entries + u64{count} * 8;
  if (count > 100000 || keys > data.size())
    return std::nullopt;

  const auto c_string = [&](u64 offset) -> std::optional<std::string_view> {
    if (offset >= data.size())
      return std::nullopt;
    const auto begin = data.begin() + static_cast<std::ptrdiff_t>(offset);
    const auto end = std::find(begin, data.end(), u8{0});
    return std::string_view(reinterpret_cast<const char*>(&*begin),
                            static_cast<size_t>(end - begin));
  };

  std::map<std::string, std::string, std::less<>> messages;
  for (u32 i = 0; i < count; ++i)
  {
    const u32 text_offset = Read32(data, entries + i * 8);
    const u32 key_offset = Read32(data, entries + i * 8 + 4);
    const std::optional<std::string_view> key = c_string(keys + key_offset);
    const std::optional<std::string_view> text = c_string(0x20 + u64{text_offset});
    if (!key || !text || text_offset >= text_size)
      continue;
    messages.insert_or_assign(SHIFTJISToUTF8(*key),
                              shift_jis ? SHIFTJISToUTF8(*text) : CP1252ToUTF8(*text));
  }
  return messages;
}

TextTables ReadDiscTables(const std::map<std::string, std::string, std::less<>>& sources,
                          const std::string& disc_path, std::string_view game_id,
                          std::vector<std::string>* errors)
{
  const auto report = [&](std::string message) {
    if (errors)
      errors->push_back(std::move(message));
  };
  // Japanese releases (game ID ending in J) store their text in Shift-JIS.
  const bool shift_jis = game_id.size() >= 4 && game_id[3] == 'J';
  TextTables tables;
  for (const auto& [name, source] : sources)
  {
    if (disc_path.empty())
    {
      report(fmt::format("table \"{}\": the game was not started from a disc image", name));
      continue;
    }
    std::optional<std::vector<u8>> data = ReadDiscFile(disc_path, source);
    if (!data)
    {
      report(fmt::format("table \"{}\": could not read {} from the disc", name, source));
      continue;
    }
    std::optional<std::map<std::string, std::string, std::less<>>> messages =
        ParseMessageTable(MaybeDecompress(std::move(*data)), shift_jis);
    if (!messages)
    {
      report(fmt::format("table \"{}\": {} is not a message table", name, source));
      continue;
    }
    auto& table = tables[name];
    for (auto& [key, text] : *messages)
      table.insert_or_assign(key, std::move(text));
  }
  return tables;
}

void InstallTextTables(Manifest& manifest, TextTables tables)
{
  for (auto& [name, entries] : tables)
  {
    auto& table = manifest.tables[name];
    for (auto& [key, text] : entries)
      table.insert_or_assign(key, std::move(text));
  }
}

void LoadDiscTables(Manifest& manifest, const std::string& disc_path, std::string_view game_id,
                    std::vector<std::string>* errors)
{
  InstallTextTables(manifest, ReadDiscTables(manifest.disc_tables, disc_path, game_id, errors));
}

namespace
{
std::mutex s_volumes_mutex;
std::map<std::string, std::unique_ptr<DiscIO::Volume>> s_volumes;
}  // namespace

void CloseDiscFiles()
{
  std::lock_guard lock(s_volumes_mutex);
  s_volumes.clear();
}

std::optional<std::vector<u8>> ReadDiscFile(const std::string& disc_path, std::string_view file)
{
  std::lock_guard lock(s_volumes_mutex);
  auto it = s_volumes.find(disc_path);
  if (it == s_volumes.end())
  {
    std::unique_ptr<DiscIO::Volume> volume = DiscIO::CreateVolume(disc_path);
    if (!volume)
      return std::nullopt;
    if (s_volumes.size() >= 2)
      s_volumes.clear();  // Keep at most a couple of discs open.
    it = s_volumes.emplace(disc_path, std::move(volume)).first;
  }
  const DiscIO::Volume& volume = *it->second;
  const DiscIO::Partition partition = volume.GetGamePartition();
  const DiscIO::FileSystem* file_system = volume.GetFileSystem(partition);
  if (!file_system)
    return std::nullopt;
  const std::unique_ptr<DiscIO::FileInfo> info = file_system->FindFileInfo(file);
  if (!info || info->IsDirectory() || info->GetSize() > MAX_FILE_SIZE)
    return std::nullopt;

  std::vector<u8> data(info->GetSize());
  if (DiscIO::ReadFile(volume, partition, info.get(), data.data(), data.size()) != data.size())
    return std::nullopt;
  return data;
}

std::optional<std::string> MaterializeDiscImage(const std::string& disc_path,
                                                const std::string& cache_directory,
                                                std::string_view source, std::string* error)
{
  const auto fail = [&](std::string message) -> std::optional<std::string> {
    if (error)
      *error = std::move(message);
    return std::nullopt;
  };
  const std::optional<DiscImageRef> ref = ParseDiscImageRef(source);
  if (!ref)
    return fail(fmt::format("\"{}\" is not a valid disc: image", source));
  if (disc_path.empty())
    return fail("the running game was not started from a disc image");

  // A readable name plus a hash, so different sources never collide.
  std::string name;
  for (char c : std::string_view(source).substr(5))
    name.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
  if (name.size() > 80)
    name.resize(80);
  const std::string path = fmt::format("{}{}_{:08x}.png", cache_directory, name,
                                       Common::ComputeCRC32(source));
  if (File::Exists(path))
    return path;

  std::optional<std::vector<u8>> data = ReadDiscFile(disc_path, ref->file);
  if (!data)
    return fail(fmt::format("could not read {} from the disc", ref->file));
  std::vector<u8> file = MaybeDecompress(std::move(*data));
  if (!ref->member.empty())
  {
    std::optional<std::vector<u8>> member = ExtractPackMember(file, ref->member);
    if (!member)
      return fail(fmt::format("{} has no member {}", ref->file, ref->member));
    file = MaybeDecompress(std::move(*member));
  }

  std::string decode_error;
  const std::optional<DecodedImage> image = DecodeTPL(file, ref->index, &decode_error);
  if (!image)
    return fail(fmt::format("{}: {}", ref->file, decode_error));

  File::CreateFullPath(cache_directory);
  const std::string temporary = path + ".tmp";
  if (!Common::SavePNG(temporary, image->rgba.data(), Common::ImageByteFormat::RGBA, image->width,
                       image->height, image->width * 4) ||
      !File::Rename(temporary, path))
  {
    return fail(fmt::format("could not write {}", path));
  }
  return path;
}
}  // namespace Duo
