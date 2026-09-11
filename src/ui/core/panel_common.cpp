#include "ui/core/core.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include "util/text.hpp"

namespace termusic::ui::core {

using namespace ftxui;

Element corePane(const std::string &title, Element content, const Theme &theme) {
  return vbox({
      text(" " + title) | bold | color(theme.accent_primary),
      text(util::repeat(70, "\u2500")) | color(theme.border),
      text(""),
      std::move(content) | flex,
  });
}

Element coreRow(std::string label, Element control, const Theme &theme) {
  return hbox({
      text(util::padRight(std::move(label), 18)) | color(theme.text),
      std::move(control) | flex,
  });
}

} // namespace termusic::ui::core
