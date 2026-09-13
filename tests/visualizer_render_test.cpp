// The visualizer's shared layers, tested where they are decidable:
//
//   * the beat envelope (shared with the onset accent),
//   * the PALETTE registry,
//   * the SPECTRUM renderer: thin independent bars (one cell of bar, one of
//     air), the absolute-height gradient, the persistent DOT baseline, the
//     light smoothing, and its runtime state (bounded by construction, cleared
//     by reset, emptied by silence -- except the dots, which are UI, not audio).
//
// The rendered cells are read back from a real FTXUI Screen, so the claims are
// measured rather than asserted about internals.
//
// One visualizer exists, so there is no registry left to test: the "which style
// is active" question no longer has an answer to check.

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>

#include "ui/visualizer/palette.hpp"
#include "ui/visualizer/renderer.hpp"
#include "visualizer/beat.hpp"

namespace {

using termusic::ui::Theme;
using termusic::ui::VisualizerFrame;
using termusic::ui::VisualizerPalette;
using termusic::ui::VisualizerRenderer;
using termusic::ui::VisualizerStats;

constexpr const char *kDot = "\u00b7";

/// Feeds a click track (an impulse every `interval` seconds) through the beat
/// detector and returns the intervals it reported.
std::vector<double> detectIntervals(double interval, int beats) {
  termusic::BeatState state;
  const double dt = 1.0 / 120.0; // 120 Hz, like a busy UI frame
  std::vector<double> detected;
  int previous_beats = 0;
  const double total = interval * static_cast<double>(beats) + 1.0;
  for (double t = 0.0; t < total; t += dt) {
    const double phase = std::fmod(t, interval);
    const double low = phase < 0.04 ? 0.85 : 0.10;
    state = termusic::beatStep(state, low, dt);
    if (state.beats > previous_beats) {
      previous_beats = state.beats;
      if (state.last_interval > 0.0)
        detected.push_back(state.last_interval);
    }
  }
  return detected;
}

double mean(const std::vector<double> &values) {
  if (values.empty())
    return 0.0;
  double sum = 0.0;
  for (const double value : values)
    sum += value;
  return sum / static_cast<double>(values.size());
}

/// One rendered frame: the renderer plus the pixels it produced.
struct Frame {
  std::vector<float> bands;
  Theme theme;
  const VisualizerPalette *palette = nullptr;
  std::unique_ptr<VisualizerRenderer> renderer;

  Frame() : renderer(termusic::ui::makeSpectrumRenderer()) {
    palette = &termusic::ui::visualizerPalette("theme");
  }

  VisualizerFrame frame(int columns, int rows, float dt, bool live,
                        float level) {
    bands.assign(96, level);
    return VisualizerFrame{.bands = bands,
                           .columns = columns,
                           .rows = rows,
                           .beat = 0.0F,
                           .dt = dt,
                           .live = live,
                           .theme = theme,
                           .palette = *palette};
  }

  /// Pushes `frames` identical frames through the renderer, then renders once
  /// into a Screen so cells can be inspected.
  ftxui::Screen settle(int columns, int rows, int frames, bool live,
                       float level) {
    VisualizerFrame f = frame(columns, rows, 1.0F / 60.0F, live, level);
    for (int index = 0; index < frames; ++index) {
      renderer->update(f);
      (void)renderer->render(f);
    }
    ftxui::Screen screen(columns, rows);
    ftxui::Render(screen, renderer->render(f));
    return screen;
  }
};

bool is_block(const std::string &glyph) {
  static const std::vector<std::string> blocks = {
      "\u2581", "\u2582", "\u2583", "\u2584",
      "\u2585", "\u2586", "\u2587", "\u2588"};
  return std::find(blocks.begin(), blocks.end(), glyph) != blocks.end();
}

bool is_dot(const std::string &glyph) { return glyph == kDot; }

} // namespace

int main() {
  // This process has no terminal, and FTXUI downgrades every RGB colour to what
  // it thinks the terminal supports -- at Palette1 the RGB values are dropped
  // entirely and every colour compares equal. Ask for truecolour explicitly, so
  // the cells carry the colours the renderer actually chose.
  ftxui::Terminal::SetColorSupport(ftxui::Terminal::Color::TrueColor);

  // --- Beat envelope (shared by the visualizer) -----------------------------
  {
    termusic::BeatState state;
    // Silence never beats, and the envelope stays at zero.
    for (int i = 0; i < 200; ++i)
      state = termusic::beatStep(state, 0.0, 1.0 / 60.0);
    assert(state.beats == 0);
    assert(state.envelope == 0.0);
    std::cout << "beat: silence produces no pulses\n";
  }
  {
    // A slow fixture (~60 BPM) and a fast one (~180 BPM) must be told apart.
    const std::vector<double> slow = detectIntervals(1.0, 12);
    const std::vector<double> fast = detectIntervals(1.0 / 3.0, 24);
    assert(slow.size() >= 8);
    assert(fast.size() >= 16);
    const double slow_mean = mean(slow);
    const double fast_mean = mean(fast);
    assert(std::abs(slow_mean - 1.0) < 0.06);
    assert(std::abs(fast_mean - 1.0 / 3.0) < 0.03);
    std::cout << "beat: 60 BPM detected at " << static_cast<int>(slow_mean * 1000)
              << " ms, 180 BPM at " << static_cast<int>(fast_mean * 1000)
              << " ms (fast pulses faster)\n";
  }

  // --- Palette registry -----------------------------------------------------
  {
    const std::vector<VisualizerPalette> &palettes =
        termusic::ui::visualizerPalettes();
    assert(palettes.size() >= 2U);
    Theme theme;
    for (const VisualizerPalette &palette : palettes) {
      assert(!palette.id.empty() && !palette.label.empty());
      assert(palette.ramp != nullptr);
      for (const float height : {0.0F, 0.5F, 1.0F})
        (void)palette.ramp(height, theme);
      assert(termusic::ui::isVisualizerPalette(palette.id));
    }
    assert(termusic::ui::normalizeVisualizerPaletteId("nonsense") == "theme");
    std::cout << "palettes: " << palettes.size()
              << " ramps, all answering for the whole height range\n";
  }

  // --- There is exactly ONE renderer, and it is the Spectrum ---------------
  {
    std::unique_ptr<VisualizerRenderer> renderer =
        termusic::ui::makeSpectrumRenderer();
    assert(renderer != nullptr);
    assert(renderer->id() == termusic::ui::kVisualizerId);
    assert(termusic::ui::kVisualizerId == "spectrum");
    std::cout << "renderer: " << renderer->id()
              << " (the only one; no style registry exists)\n";
  }

  // --- The gradient is keyed on ABSOLUTE height ----------------------------
  {
    const Theme &theme = Theme{};
    const VisualizerPalette &palette = termusic::ui::visualizerPalette("theme");
    const ftxui::Color low = termusic::ui::spectrumColor(0.0F, palette, theme);
    const ftxui::Color mid = termusic::ui::spectrumColor(0.5F, palette, theme);
    const ftxui::Color high = termusic::ui::spectrumColor(1.0F, palette, theme);
    assert(low != high && "the height must change the colour");
    assert(mid != low && mid != high);
    // The ramp is a function of the height ALONE.
    for (const float height : {0.0F, 0.25F, 0.5F, 0.75F, 1.0F}) {
      assert(termusic::ui::spectrumColor(height, palette, theme) ==
             termusic::ui::spectrumColor(height, palette, theme));
    }
    // Every palette reaches the cells, so the setting still recolours it.
    Theme other;
    other.spectrum_low = ftxui::Color::RGB(0x00, 0xFF, 0x00);
    other.spectrum_high = ftxui::Color::RGB(0x00, 0x00, 0xFF);
    other.spectrum_peak = ftxui::Color::RGB(0xFF, 0x00, 0x00);
    assert(termusic::ui::spectrumColor(1.0F, palette, other) != high);
    std::cout << "gradient: the height fraction alone decides the colour, and "
                 "the theme supplies it\n";
  }

  // --- Geometry, the bar pattern and the dot baseline ----------------------
  {
    Frame fixture;
    const int columns = 61;
    const int rows = 17;
    const int dot_row = rows - 1;
    ftxui::Screen screen = fixture.settle(columns, rows, 180, true, 0.85F);
    const VisualizerStats stats = fixture.renderer->stats();

    // 1. The object sits LOW: it grows up from the dot row at the bottom of the
    //    drawable area, and the height it may use is under two thirds.
    assert(stats.baseline == dot_row && "the dots are the bottom row");
    assert(stats.main_rows > rows / 2 &&
           "the bars may use more than half the drawable height");
    assert(stats.main_rows <= (dot_row * 62) / 100 + 1);
    assert(stats.drawn > 0 && "a loud signal must be visible");

    // 2. BAR, SPACE, BAR: the bars are one cell wide, one cell apart.
    const int bars = stats.bars;
    assert(bars > 10 && "a wide signal must fill the width with bars");
    assert(stats.dots == bars && "one dot per bar, always");
    int first_bar = columns;
    int last_bar = -1;
    int bar_cells = 0;
    for (int x = 0; x < columns; ++x) {
      if (!is_block(screen.CellAt(x, dot_row - 1).character))
        continue;
      first_bar = std::min(first_bar, x);
      last_bar = std::max(last_bar, x);
      ++bar_cells;
    }
    assert(first_bar < last_bar && bar_cells > 10);
    for (int bar = 0; bar < bars; ++bar) {
      const int x = first_bar + bar * 2;
      assert(x <= last_bar && "every bar must be inside the object");
      // The bar's cell carries ink and the gap after it does not.
      assert(is_dot(screen.CellAt(x, dot_row).character) &&
             "every bar has its dot on the dot row");
      if (x + 1 <= last_bar)
        assert(!is_block(screen.CellAt(x + 1, dot_row - 1).character) &&
               "the bar after a gap must not touch it");
    }
    int dots = 0;
    for (int x = 0; x < columns; ++x) {
      if (is_dot(screen.CellAt(x, dot_row).character))
        ++dots;
    }
    assert(dots == bars && "the dot count equals the bar count");
    // The dots are the palette's LOW accent, not a peak colour.
    const Theme &theme = fixture.theme;
    const VisualizerPalette &palette = *fixture.palette;
    const ftxui::Color dot_colour = screen.CellAt(first_bar, dot_row).foreground_color;
    assert(dot_colour == palette.ramp(0.0F, theme) &&
           "the dots use the palette's low accent");
    assert(dot_colour != palette.ramp(1.0F, theme) &&
           "the dots are weaker than the peaks");

    // 3. Centred, and wide: about 90% of the drawable width here, with the same
    //    margin on both sides.
    const int object_width = last_bar - first_bar + 1;
    const int left_margin = first_bar;
    const int right_margin = columns - 1 - last_bar;
    assert(left_margin > 0 && right_margin > 0 && "both margins must exist");
    assert(std::abs(left_margin - right_margin) <= 1 &&
           "the object is centred");
    const float share =
        static_cast<float>(object_width) / static_cast<float>(columns);
    assert(share >= 0.85F && share <= 0.95F &&
           "the bars use most of the drawable width");

    // 4. The bars stay clear of the dot row and inside the drawable area.
    for (int y = dot_row + 1; y < rows; ++y) {
      for (int x = 0; x < columns; ++x) {
        assert(!is_block(screen.CellAt(x, y).character) &&
               "nothing is drawn below the dots");
      }
    }

    // 5. The gradient is ABSOLUTE and the same row is one colour.
    int top_row = rows;
    for (int y = 0; y < dot_row && top_row == rows; ++y) {
      for (int x = 0; x < columns; ++x) {
        if (is_block(screen.CellAt(x, y).character)) {
          top_row = y;
          break;
        }
      }
    }
    assert(top_row < dot_row);
    std::vector<int> drawn_columns;
    for (int x = 0; x < columns; ++x) {
      if (is_block(screen.CellAt(x, dot_row - 1).character))
        drawn_columns.push_back(x);
    }
    const ftxui::Color bottom_colour =
        screen.CellAt(drawn_columns.front(), dot_row - 1).foreground_color;
    for (const int x : drawn_columns) {
      assert(screen.CellAt(x, dot_row - 1).foreground_color == bottom_colour &&
             "one row is one colour: the height decides it, not the bar");
    }
    assert(bottom_colour ==
           termusic::ui::spectrumColor(
               0.5F / static_cast<float>(stats.main_rows), palette, theme) &&
           "the bottom row carries the low end of the ramp");
    std::cout << "bars: " << bars << " bars of 1 cell with 1 cell of air, "
              << dots << " dots, object " << object_width << "/" << columns
              << " columns ("
              << static_cast<int>(share * 100.0F + 0.5F) << "%), dot row "
              << stats.baseline << " of " << rows << ", bar area "
              << stats.main_rows << " rows\n";

    // 6. Distribution: the ends are compressed, the centre carries the peaks,
    //    and a saturated band is not clipped flat.
    {
      std::vector<float> quarter(96, 0.0F);
      for (std::size_t index = 0; index < 24; ++index)
        quarter[index] = 1.0F;
      std::unique_ptr<VisualizerRenderer> shaped =
          termusic::ui::makeSpectrumRenderer();
      VisualizerFrame quarter_frame{.bands = quarter,
                                    .columns = columns,
                                    .rows = rows,
                                    .beat = 0.0F,
                                    .dt = 1.0F / 60.0F,
                                    .live = true,
                                    .theme = theme,
                                    .palette = palette};
      for (int index = 0; index < 240; ++index) {
        shaped->update(quarter_frame);
        (void)shaped->render(quarter_frame);
      }
      ftxui::Screen quarter_screen(columns, rows);
      ftxui::Render(quarter_screen, shaped->render(quarter_frame));
      int loud_left = columns;
      int loud_right = -1;
      int loud_height = 0;
      for (int y = 0; y < dot_row; ++y) {
        for (int x = 0; x < columns; ++x) {
          if (!is_block(quarter_screen.CellAt(x, y).character))
            continue;
          loud_left = std::min(loud_left, x);
          loud_right = std::max(loud_right, x);
          loud_height = std::max(loud_height, dot_row - y);
        }
      }
      int cliff = loud_left;
      for (int x = loud_left; x < columns; ++x) {
        int height = 0;
        for (int y = 0; y < dot_row; ++y) {
          if (is_block(quarter_screen.CellAt(x, y).character)) {
            height = dot_row - y;
            break;
          }
        }
        if (height * 2 >= loud_height)
          cliff = x;
      }
      const float ink_share = static_cast<float>(cliff - loud_left + 1) /
                              static_cast<float>(object_width);
      assert(ink_share > 0.15F && ink_share < 0.48F &&
             "a quarter-loud signal must fall away well before the middle");
      std::cout << "distribution: a quarter-loud signal falls to half height at "
                << static_cast<int>(ink_share * 100.0F + 0.5F)
                << "% of the object\n";

      std::vector<float> saturated(96, 1.0F);
      std::unique_ptr<VisualizerRenderer> loud =
          termusic::ui::makeSpectrumRenderer();
      VisualizerFrame loud_frame{.bands = saturated,
                                 .columns = columns,
                                 .rows = rows,
                                 .beat = 0.0F,
                                 .dt = 1.0F / 60.0F,
                                 .live = true,
                                 .theme = theme,
                                 .palette = palette};
      for (int index = 0; index < 240; ++index) {
        loud->update(loud_frame);
        (void)loud->render(loud_frame);
      }
      ftxui::Screen loud_screen(columns, rows);
      ftxui::Render(loud_screen, loud->render(loud_frame));
      int saturated_top = rows;
      for (int y = 0; y < dot_row && saturated_top == rows; ++y) {
        for (int x = 0; x < columns; ++x) {
          if (is_block(loud_screen.CellAt(x, y).character)) {
            saturated_top = y;
            break;
          }
        }
      }
      int flat = 0;
      for (int x = 0; x < columns; ++x) {
        if (loud_screen.CellAt(x, saturated_top).character == "\u2588")
          ++flat;
      }
      assert(flat == 0 &&
             "a saturated band must not end in full blocks on one row: that "
             "flat plateau is what reads as clipping");
      std::cout << "distribution: a saturated signal tops out on partial "
                   "blocks (no clipped plateau)\n";
    }

    // 7. Smoothing: a comb signal is drawn with a gentler step between
    //    neighbouring bars than the raw band values would give.
    {
      std::vector<float> comb(96, 0.2F);
      for (std::size_t index = 0; index < comb.size(); index += 4)
        comb[index] = 1.0F;
      std::unique_ptr<VisualizerRenderer> smoothed =
          termusic::ui::makeSpectrumRenderer();
      VisualizerFrame comb_frame{.bands = comb,
                                 .columns = columns,
                                 .rows = rows,
                                 .beat = 0.0F,
                                 .dt = 1.0F / 60.0F,
                                 .live = true,
                                 .theme = theme,
                                 .palette = palette};
      for (int index = 0; index < 300; ++index) {
        smoothed->update(comb_frame);
        (void)smoothed->render(comb_frame);
      }
      ftxui::Screen comb_screen(columns, rows);
      ftxui::Render(comb_screen, smoothed->render(comb_frame));
      const auto height_at = [&](int x) {
        for (int y = 0; y < dot_row; ++y) {
          if (is_block(comb_screen.CellAt(x, y).character))
            return dot_row - y;
        }
        return 0;
      };
      int steps = 0;
      int step_sum = 0;
      int tallest = 0;
      for (int bar = 1; bar < bars; ++bar) {
        const int x = first_bar + bar * 2;
        const int previous = first_bar + (bar - 1) * 2;
        if (x > last_bar)
          break;
        step_sum += std::abs(height_at(x) - height_at(previous));
        ++steps;
        tallest = std::max(tallest, height_at(x));
      }
      const float mean_step =
          steps > 0 ? static_cast<float>(step_sum) / static_cast<float>(steps)
                    : 0.0F;
      assert(mean_step < 0.45F * static_cast<float>(stats.main_rows) &&
             "the contour is not smoothed: neighbouring bars still jump by "
             "most of the bar area");
      assert(tallest >= stats.main_rows / 2 &&
             "smoothing must not flatten the peaks away");
      std::cout << "smoothing: a comb signal steps " << mean_step
                << " rows between neighbours (bar area " << stats.main_rows
                << "), peaks still reach " << tallest << "\n";
    }

    // 8. The dots are UI, not audio: they survive every silent state, and only
    //    the bars go away.
    {
      // A pause: the bars decay, the dots stay.
      VisualizerFrame quiet = fixture.frame(columns, rows, 1.0F / 60.0F, false,
                                            0.0F);
      fixture.renderer->update(quiet);
      fixture.renderer->render(quiet);
      const VisualizerStats decaying = fixture.renderer->stats();
      assert(decaying.dots == bars && "the dots never blink out");
      assert(decaying.drawn > decaying.dots &&
             "a paused frame still shows the bars that have not decayed yet");
      // A full stop: after the decay, only the dots are left.
      VisualizerStats stopped;
      for (int index = 0; index < 240; ++index) {
        VisualizerFrame silent = fixture.frame(columns, rows, 1.0F / 60.0F,
                                               false, 0.0F);
        fixture.renderer->update(silent);
        (void)fixture.renderer->render(silent);
        stopped = fixture.renderer->stats();
      }
      assert(stopped.retained == 0 && "the bars are gone");
      assert(stopped.drawn == stopped.dots &&
             "a stopped frame is the dot baseline and nothing else");
      ftxui::Screen stopped_screen(columns, rows);
      ftxui::Render(stopped_screen, fixture.renderer->render(quiet));
      int still_dots = 0;
      int any_bar = 0;
      for (int x = 0; x < columns; ++x) {
        if (is_dot(stopped_screen.CellAt(x, dot_row).character))
          ++still_dots;
        for (int y = 0; y < dot_row; ++y)
          any_bar += is_block(stopped_screen.CellAt(x, y).character) ? 1 : 0;
      }
      assert(still_dots == bars && "the dot baseline is still complete");
      assert(any_bar == 0 && "no bar survives a stopped player");
      std::cout << "dots: " << still_dots
                << " dots remain with no signal, and no bars are drawn\n";
    }

    // 9. Reset: a reconfiguration starts from nothing.
    fixture.renderer->reset();
    const VisualizerStats fresh = fixture.renderer->stats();
    assert(fresh.retained == 0 && fresh.drawn == 0);

    // 10. Resizing: the pattern, the centring and the dot count hold at every
    //     size.
    for (int step = 0; step < 40; ++step) {
      const int width = 24 + (step * 7) % 80;
      const int height = 6 + (step * 3) % 30;
      VisualizerFrame resized = fixture.frame(width, height, 1.0F / 60.0F, true,
                                              0.7F);
      fixture.renderer->update(resized);
      ftxui::Screen small(width, height);
      ftxui::Render(small, fixture.renderer->render(resized));
      const VisualizerStats grown = fixture.renderer->stats();
      assert(grown.retained <= grown.capacity);
      assert(grown.drawn <= width * height);
      assert(grown.dots == grown.bars && "one dot per bar at every size");
      assert(grown.baseline == height - 1 && "the dots stay on the bottom row");
      assert(grown.bars >= 1);
      int seen_dots = 0;
      for (int x = 0; x < width; ++x)
        seen_dots += is_dot(small.CellAt(x, height - 1).character) ? 1 : 0;
      assert(seen_dots == grown.bars && "the drawn dots match the bar count");
    }
    std::cout << "state: bounded, cleared by reset, dots always drawn\n";
  }

  // --- The palette is what colours the screen ------------------------------
  {
    Frame fixture;
    const int columns = 41;
    const int rows = 15;
    const ftxui::Screen themed = fixture.settle(columns, rows, 120, true, 0.9F);
    const int probe_x = columns / 2;
    const int probe_y = rows - 2;
    const ftxui::Color theme_colour =
        themed.CellAt(probe_x, probe_y).foreground_color;
    fixture.palette = &termusic::ui::visualizerPalette("ice");
    const ftxui::Screen iced = fixture.settle(columns, rows, 120, true, 0.9F);
    const ftxui::Color ice_colour =
        iced.CellAt(probe_x, probe_y).foreground_color;
    assert(theme_colour != ice_colour &&
           "the palette setting must still recolour the Spectrum");
    std::cout << "palette: the selected ramp is what reaches the cells\n";
  }

  std::cout << "visualizer: all checks passed\n";
  return 0;
}
