#include "Capture.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "GameData.hpp"
#include "pudforge/pudforge.h"
#include "version.h"

namespace pfwin {
namespace {

/// A GUI-subsystem process has no console of its own. When it was launched from
/// one, attaching to the parent's is the difference between a tool that reports
/// what went wrong and one that fails in silence.
struct ParentConsole {
  bool attached = false;
  ParentConsole() {
    attached = AttachConsole(ATTACH_PARENT_PROCESS) != 0;
    if (!attached) return;
    // Only when the stream is not already going somewhere. Reopening on CONOUT$
    // unconditionally writes to the console *instead of* a redirect, so
    // `PUDForge --version > out.txt` left an empty file — the one thing a
    // version flag exists not to do.
    FILE* unused = nullptr;
    if (!Redirected(STD_OUTPUT_HANDLE)) freopen_s(&unused, "CONOUT$", "w", stdout);
    if (!Redirected(STD_ERROR_HANDLE)) freopen_s(&unused, "CONOUT$", "w", stderr);
  }
  ~ParentConsole() {
    if (attached) FreeConsole();
  }

 private:
  static bool Redirected(DWORD which) {
    const HANDLE handle = GetStdHandle(which);
    return handle && handle != INVALID_HANDLE_VALUE;
  }
};

void Say(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stdout, format, args);
  va_end(args);
  fflush(stdout);
}

constexpr const char* kUsage =
    "usage: PUDForge --render <map.pud> [options]\n"
    "       PUDForge --tilesheet <0-3> [options]\n"
    "       PUDForge --version\n"
    "\n"
    "  --out <file.png>     output path (default render.png)\n"
    "  --tiles x,y,w,h      render only this tile rect (default whole map)\n"
    "  --scale <n>          integer pixel scale, 1 = 32 px per tile\n"
    "  --no-units           terrain only\n"
    "  --grid               overlay a one-pixel tile grid\n"
    "  --overlay <0-3>      art / movement / regions / tiles\n"
    "  --data <dir>         Warcraft II folder (default: the usual search)\n"
    "  --columns <n>        tilesheet only: megatiles per row (default 16)\n"
    "\n"
    "Writes a PNG without creating a window. Exit status is non-zero on\n"
    "failure, so this is usable as a check.\n";

struct Args {
  std::wstring map;
  std::wstring out = L"render.png";
  std::wstring data;
  int x = 0, y = 0, w = 0, h = 0;
  bool have_rect = false;
  int scale = 1;
  int overlay = 0;
  int columns = 16;
  int tilesheet = -1;
  bool units = true;
  bool grid = false;
};

bool ParseRect(const std::wstring& text, Args& args) {
  int values[4] = {0, 0, 0, 0};
  size_t at = 0;
  for (int i = 0; i < 4; i++) {
    if (at >= text.size()) return false;
    size_t used = 0;
    values[i] = std::stoi(text.substr(at), &used);
    at += used;
    if (i < 3) {
      if (at >= text.size() || text[at] != L',') return false;
      at++;
    }
  }
  args.x = values[0];
  args.y = values[1];
  args.w = values[2];
  args.h = values[3];
  args.have_rect = args.w > 0 && args.h > 0;
  return args.have_rect;
}

/// Scale a composed buffer by whole pixels. Nearest-neighbour on purpose: the
/// artwork is 32x32 pixel art and smoothing it would be a lie about the map.
std::vector<uint32_t> Magnify(const std::vector<uint32_t>& src, int w, int h,
                              int scale) {
  if (scale <= 1) return src;
  std::vector<uint32_t> out(size_t(w) * size_t(h) * size_t(scale) * size_t(scale));
  const int dw = w * scale;
  for (int y = 0; y < h * scale; y++) {
    const uint32_t* in = src.data() + size_t(y / scale) * size_t(w);
    uint32_t* row = out.data() + size_t(y) * size_t(dw);
    for (int x = 0; x < dw; x++) row[x] = in[x / scale];
  }
  return out;
}

bool WritePng(const std::wstring& path, const std::vector<uint32_t>& pixels,
              int w, int h) {
  size_t length = 0;
  uint8_t* png = pf_png_encode(pixels.data(), w, h, &length);
  if (!png) return false;
  FILE* file = nullptr;
  const errno_t err = _wfopen_s(&file, path.c_str(), L"wb");
  bool ok = err == 0 && file != nullptr;
  if (ok) {
    ok = fwrite(png, 1, length, file) == length;
    fclose(file);
  }
  pf_buffer_free(png);
  return ok;
}

int RenderMap(const Args& args, GameData& game) {
  pf_status status = PF_OK;
  const std::string utf8 = ToUtf8(args.map);
  pf_map* map = pf_map_open_file(utf8.c_str(), &status);
  if (!map) {
    Say("cannot open %s (status %d)\n", utf8.c_str(), int(status));
    return 1;
  }

  int x0 = args.x, y0 = args.y;
  int cols = args.have_rect ? args.w : pf_map_width(map);
  int rows = args.have_rect ? args.h : pf_map_height(map);
  if (x0 < 0 || y0 < 0 || cols <= 0 || rows <= 0 ||
      x0 + cols > pf_map_width(map) || y0 + rows > pf_map_height(map)) {
    Say("the tile rectangle is not inside a %dx%d map\n", pf_map_width(map),
        pf_map_height(map));
    pf_map_free(map);
    return 2;
  }

  pf_tileset_art* art = game.OpenTileset(pf_map_tileset(map));
  pf_sprite_set* sprites =
      args.units && art ? game.OpenSprites(map, art) : nullptr;
  if (!art) {
    // Not fatal, and worth saying: flat colours still show the shape of a map
    // and every rule about it still holds.
    Say("no game artwork found; drawing flat terrain colours\n");
  }

  pf_render_options options = {};
  options.x0 = x0;
  options.y0 = y0;
  options.cols = cols;
  options.rows = rows;
  options.art = art;
  options.sprites = sprites;
  options.overlay = args.overlay;
  options.grid = args.grid ? 1 : 0;
  options.placeholders = 1;
  // Every unit facing the same way, so two captures of one map can be compared.
  options.vary_facing = 0;

  const int needed = pf_map_compose_region(map, &options, nullptr, 0);
  std::vector<uint32_t> pixels(size_t(needed > 0 ? needed : 0));
  if (needed <= 0 ||
      pf_map_compose_region(map, &options, pixels.data(), pixels.size()) != needed) {
    Say("composition failed\n");
    if (sprites) pf_sprite_set_free(sprites);
    if (art) pf_tileset_art_free(art);
    pf_map_free(map);
    return 3;
  }

  const int pw = cols * 32, ph = rows * 32;
  const std::vector<uint32_t> scaled = Magnify(pixels, pw, ph, args.scale);
  const bool ok = WritePng(args.out, scaled, pw * args.scale, ph * args.scale);
  if (ok) {
    Say("wrote %ls  %dx%d px, tiles %d,%d %dx%d\n", args.out.c_str(),
        pw * args.scale, ph * args.scale, x0, y0, cols, rows);
  } else {
    Say("cannot write %ls\n", args.out.c_str());
  }

  if (sprites) pf_sprite_set_free(sprites);
  if (art) pf_tileset_art_free(art);
  pf_map_free(map);
  return ok ? 0 : 4;
}

int RenderTilesheet(const Args& args, GameData& game) {
  pf_tileset_art* art = game.OpenTileset(args.tilesheet);
  if (!art) {
    Say("no artwork for tileset %d. Is the game folder right?\n", args.tilesheet);
    return 1;
  }
  const int count = pf_tileset_art_megatile_count(art);
  const int columns = args.columns > 0 ? args.columns : 16;
  const int rows = (count + columns - 1) / columns;
  const int pw = columns * 32, ph = rows * 32;

  // Every megatile the tileset holds, laid out in a grid. This is the check that
  // separates "the artwork decodes wrong" from "the map picks the wrong tiles".
  std::vector<uint32_t> sheet(size_t(pw) * size_t(ph), 0xff000000u);
  for (int i = 0; i < count; i++) {
    const int ox = (i % columns) * 32, oy = (i / columns) * 32;
    // The last argument is the destination's stride in pixels, not a capacity,
    // so each megatile can be drawn straight into its place in the sheet.
    pf_tileset_art_draw(art, i, sheet.data() + size_t(oy) * size_t(pw) + size_t(ox),
                        pw);
  }
  const std::vector<uint32_t> scaled = Magnify(sheet, pw, ph, args.scale);
  const bool ok = WritePng(args.out, scaled, pw * args.scale, ph * args.scale);
  Say(ok ? "wrote %ls  %d megatiles\n" : "cannot write %ls (%d megatiles)\n",
      args.out.c_str(), count);
  pf_tileset_art_free(art);
  return ok ? 0 : 4;
}

}  // namespace

bool WantsCapture(int argc, wchar_t** argv) {
  for (int i = 1; i < argc; i++) {
    const std::wstring arg = argv[i];
    // --version belongs with these rather than with the window: what a script
    // asking it wants is a line on stdout and an exit code, and this is the path
    // that has a console attached and returns before anything is created.
    if (arg == L"--render" || arg == L"--tilesheet" || arg == L"--help" ||
        arg == L"--version") {
      return true;
    }
  }
  return false;
}

int RunCapture(int argc, wchar_t** argv) {
  ParentConsole console;
  Args args;

  for (int i = 1; i < argc; i++) {
    const std::wstring arg = argv[i];
    auto value = [&](const wchar_t* name) -> std::wstring {
      if (i + 1 >= argc) {
        Say("%ls needs a value\n", name);
        exit(2);
      }
      return argv[++i];
    };
    if (arg == L"--help") { Say("%s", kUsage); return 0; }
    else if (arg == L"--version") {
      // Both numbers, because the client and the core version separately and
      // "which build is this" usually means both of them.
      Say("PUDForge %s (core %s)\n", PF_APP_VERSION_STR, pf_version());
      return 0;
    }
    else if (arg == L"--render") args.map = value(L"--render");
    else if (arg == L"--tilesheet") args.tilesheet = _wtoi(value(L"--tilesheet").c_str());
    else if (arg == L"--out") args.out = value(L"--out");
    else if (arg == L"--data") args.data = value(L"--data");
    else if (arg == L"--scale") args.scale = _wtoi(value(L"--scale").c_str());
    else if (arg == L"--overlay") args.overlay = _wtoi(value(L"--overlay").c_str());
    else if (arg == L"--columns") args.columns = _wtoi(value(L"--columns").c_str());
    else if (arg == L"--no-units") args.units = false;
    else if (arg == L"--grid") args.grid = true;
    else if (arg == L"--tiles") {
      if (!ParseRect(value(L"--tiles"), args)) {
        Say("--tiles wants x,y,w,h with a positive size\n");
        return 2;
      }
    } else {
      Say("unknown option %ls\n\n%s", arg.c_str(), kUsage);
      return 2;
    }
  }
  if (args.scale < 1 || args.scale > 8) {
    Say("--scale wants 1 to 8\n");
    return 2;
  }

  GameData game;
  if (!args.data.empty()) {
    // An explicit folder must not be silently ignored: a capture drawn in flat
    // colours because a path was wrong looks like a rendering bug.
    if (!game.Adopt(args.data)) {
      Say("no Warcraft II data in %ls\n", args.data.c_str());
      return 1;
    }
  } else {
    game.Restore();
  }

  if (args.tilesheet >= 0) {
    if (args.tilesheet > 3) { Say("--tilesheet wants 0 to 3\n"); return 2; }
    return RenderTilesheet(args, game);
  }
  if (args.map.empty()) { Say("%s", kUsage); return 2; }
  return RenderMap(args, game);
}

}  // namespace pfwin
