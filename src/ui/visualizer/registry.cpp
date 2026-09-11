#include "ui/visualizer/renderer.hpp"

#include <algorithm>

namespace termusic::ui {

std::unique_ptr<VisualizerRenderer> makeClassicBarsRenderer();
std::unique_ptr<VisualizerRenderer> makeWaterfallRenderer();
std::unique_ptr<VisualizerRenderer> makeParticlesRenderer();

namespace {

/// THE STYLE REGISTRY. Exactly three styles; there is no legacy alias here, and
/// the canonical fallback below is Classic Bars rather than anything that used
/// to exist.
const std::vector<VisualizerStyle> kStyles = {
    {"classic-bars", "Classic Bars", makeClassicBarsRenderer},
    {"waterfall", "Waterfall", makeWaterfallRenderer},
    {"particles", "Particles", makeParticlesRenderer},
};

} // namespace

const std::vector<VisualizerStyle> &visualizerStyles() { return kStyles; }

std::string_view defaultVisualizerStyleId() { return kStyles.front().id; }

bool isVisualizerStyle(std::string_view id) {
  return std::any_of(kStyles.begin(), kStyles.end(),
                     [id](const VisualizerStyle &style) {
                       return style.id == id;
                     });
}

std::string_view normalizeVisualizerStyleId(std::string_view id) {
  // One rule: anything the registry does not know becomes the canonical style.
  // That is what turns a stored "city" (or any other removed identifier) into
  // Classic Bars without resurrecting the renderer it named.
  return isVisualizerStyle(id) ? id : defaultVisualizerStyleId();
}

std::unique_ptr<VisualizerRenderer> makeVisualizerRenderer(std::string_view id) {
  const std::string_view resolved = normalizeVisualizerStyleId(id);
  for (const VisualizerStyle &style : kStyles) {
    if (style.id == resolved)
      return style.make();
  }
  // Unreachable: the normalized id always names a registry entry. Keeping the
  // first style as the answer means a factory can never hand back nullptr.
  return kStyles.front().make();
}

std::string_view visualizerStyleLabel(std::string_view id) {
  const std::string_view resolved = normalizeVisualizerStyleId(id);
  for (const VisualizerStyle &style : kStyles) {
    if (style.id == resolved)
      return style.label;
  }
  return kStyles.front().label;
}

} // namespace termusic::ui
