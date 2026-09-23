#include "Description.h"

#include <Epub/BookMetadataCache.h>
#include <GfxRenderer.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <utility>

#include "state/RecentBooks.h"
#include "images/Star.h"
#include "MetadataRating.h"
#include "system/Fonts.h"

namespace {

void appendUtf8(std::string& output, const uint32_t codepoint) {
  if (codepoint <= 0x7F) {
    output.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    output.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    output.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    output.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    output.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

bool appendHtmlEntity(const std::string& entity, std::string& output) {
  if (entity == "amp") output += '&';
  else if (entity == "lt") output += '<';
  else if (entity == "gt") output += '>';
  else if (entity == "quot") output += '"';
  else if (entity == "apos" || entity == "#39") output += '\'';
  else if (entity == "nbsp") output += ' ';
  else if (entity == "ndash") appendUtf8(output, 0x2013);
  else if (entity == "mdash") appendUtf8(output, 0x2014);
  else if (entity == "hellip") appendUtf8(output, 0x2026);
  else if (entity == "ldquo") appendUtf8(output, 0x201C);
  else if (entity == "rdquo") appendUtf8(output, 0x201D);
  else if (entity == "lsquo") appendUtf8(output, 0x2018);
  else if (entity == "rsquo") appendUtf8(output, 0x2019);
  else if (entity.size() > 1 && entity[0] == '#') {
    char* end = nullptr;
    const int base = entity.size() > 2 && (entity[1] == 'x' || entity[1] == 'X') ? 16 : 10;
    const char* number = entity.c_str() + (base == 16 ? 2 : 1);
    const unsigned long codepoint = std::strtoul(number, &end, base);
    if (!end || *end != '\0' || codepoint > 0x10FFFF) return false;
    appendUtf8(output, static_cast<uint32_t>(codepoint));
  } else {
    return false;
  }
  return true;
}

EpdFontFamily::Style descriptionStyle(const bool italic) {
  return italic ? EpdFontFamily::ITALIC : EpdFontFamily::REGULAR;
}

void appendRun(std::vector<DescriptionWidgetLine::Run>& runs, const std::string& text,
               const EpdFontFamily::Style style) {
  if (text.empty()) return;
  if (!runs.empty() && runs.back().style == style) {
    runs.back().text += text;
  } else {
    DescriptionWidgetLine::Run run;
    run.text = text;
    run.style = style;
    runs.push_back(std::move(run));
  }
}

std::vector<DescriptionWidgetLine::Run> parseDescription(const std::string& raw) {
  std::vector<DescriptionWidgetLine::Run> runs;
  std::string text;
  bool italic = false;
  const auto flush = [&] {
    appendRun(runs, text, descriptionStyle(italic));
    text.clear();
  };

  for (size_t i = 0; i < raw.size();) {
    if (raw[i] == '<') {
      const size_t close = raw.find('>', i + 1);
      if (close == std::string::npos) {
        text.push_back(raw[i++]);
        continue;
      }
      std::string tag = raw.substr(i + 1, close - i - 1);
      size_t start = 0;
      while (start < tag.size() && std::isspace(static_cast<unsigned char>(tag[start]))) ++start;
      const bool closing = start < tag.size() && tag[start] == '/';
      if (closing) ++start;
      while (start < tag.size() && std::isspace(static_cast<unsigned char>(tag[start]))) ++start;
      size_t end = start;
      while (end < tag.size() && std::isalpha(static_cast<unsigned char>(tag[end]))) ++end;
      std::string name = tag.substr(start, end - start);
      for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      const bool lineBreak = name == "br" || name == "p" || name == "div" || name == "li";
      flush();
      if (lineBreak && (name == "br" || closing)) appendRun(runs, "\n", descriptionStyle(italic));
      if (name == "i" || name == "em") italic = !closing;
      i = close + 1;
    } else if (raw[i] == '&') {
      const size_t semi = raw.find(';', i + 1);
      if (semi != std::string::npos && appendHtmlEntity(raw.substr(i + 1, semi - i - 1), text)) {
        i = semi + 1;
      } else {
        text.push_back(raw[i++]);
      }
    } else {
      text.push_back(raw[i++]);
    }
  }
  flush();
  return runs;
}

std::string metadataCachePath(const RecentBook& book) {
  if (!book.cachePath.empty()) return book.cachePath;
  return "/.metadata/epub/" + std::to_string(std::hash<std::string>{}(book.path));
}

std::string bookTitle(const RecentBook& book) {
  if (!book.title.empty()) return book.title;
  const size_t slash = book.path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = book.path.find_last_of('.');
  const size_t end = dot == std::string::npos || dot < start ? book.path.size() : dot;
  return book.path.substr(start, end - start);
}

void renderProgress(GfxRenderer& renderer, const int x, const int y, const int width, const int percentage) {
  constexpr int barHeight = 5;
  constexpr int percentageFont = MONTSERRAT_8_FONT_ID;
  constexpr int gap = 8;
  const int barWidth = std::max(1, width / 2);
  const std::string percentageText = std::to_string(percentage) + "%";
  renderer.rectangle.render(x, y, barWidth, barHeight, true);
  renderer.rectangle.fill(x + 1, y + 1, std::max(1, barWidth - 2), barHeight - 2, false);
  if (percentage > 0) {
    renderer.rectangle.fill(x + 1, y + 1, std::max(1, (barWidth - 2) * percentage / 100), barHeight - 2, true);
  }
  const int percentageY = y + (barHeight - renderer.text.getLineHeight(percentageFont)) / 2;
  renderer.text.render(percentageFont, x + barWidth + gap, percentageY, percentageText.c_str(), true);
}

std::vector<DescriptionWidgetLine> wrapDescription(const GfxRenderer& renderer, const std::string& raw,
                                                    const int width) {
  std::vector<DescriptionWidgetLine> lines;
  if (raw.empty()) return lines;

  constexpr int font = MONTSERRAT_12_FONT_ID;
  const int spaceWidth = renderer.text.getSpaceWidth(font);
  const std::vector<DescriptionWidgetLine::Run> runs = parseDescription(raw);
  DescriptionWidgetLine line;
  int lineWidth = 0;

  const auto pushLine = [&] {
    if (!line.runs.empty() && !line.runs.back().text.empty() && line.runs.back().text.back() == ' ') {
      line.runs.back().text.pop_back();
    }
    lines.push_back(std::move(line));
    line = DescriptionWidgetLine();
    lineWidth = 0;
  };

  for (const DescriptionWidgetLine::Run& run : runs) {
    size_t pos = 0;
    while (pos <= run.text.size()) {
      const size_t newline = run.text.find('\n', pos);
      const size_t end = newline == std::string::npos ? run.text.size() : newline;
      size_t token = pos;
      while (token < end) {
        while (token < end && std::isspace(static_cast<unsigned char>(run.text[token]))) ++token;
        if (token >= end) break;
        size_t tokenEnd = token;
        while (tokenEnd < end && !std::isspace(static_cast<unsigned char>(run.text[tokenEnd]))) ++tokenEnd;
        const std::string word = run.text.substr(token, tokenEnd - token);
        const int wordWidth = renderer.text.getWidth(font, word.c_str(), run.style);
        const int required = line.runs.empty() ? wordWidth : spaceWidth + wordWidth;
        if (!line.runs.empty() && lineWidth + required > width) pushLine();
        if (line.runs.empty() && wordWidth > width) {
          appendRun(line.runs, renderer.text.truncate(font, word.c_str(), width, run.style), run.style);
          pushLine();
        } else {
          if (!line.runs.empty()) {
            appendRun(line.runs, " ", run.style);
            lineWidth += spaceWidth;
          }
          appendRun(line.runs, word, run.style);
          lineWidth += wordWidth;
        }
        token = tokenEnd;
      }
      if (newline == std::string::npos) break;
      pushLine();
      pos = newline + 1;
    }
  }
  if (!line.runs.empty()) pushLine();
  return lines;
}

}  // namespace

void Description::ensureLines(const int recentIndex, const int width) const {
  const auto& books = RECENT_BOOKS.getBooks();
  const std::string path = recentIndex >= 0 && recentIndex < static_cast<int>(books.size())
                               ? books[static_cast<size_t>(recentIndex)].path
                               : std::string();
  if (path == cachedPath_ && width == cachedWidth_) return;

  cachedPath_ = path;
  cachedWidth_ = width;
  lines_.clear();
  cachedRatingStars_ = 0;

  std::string raw;
  if (!path.empty()) {
    const RecentBook& book = books[static_cast<size_t>(recentIndex)];
    BookMetadataCache metadata(metadataCachePath(book));
    if (metadata.load()) {
      raw = metadata.coreMetadata.description;
      cachedRatingStars_ = metadataRatingStars(metadata.coreMetadata.rating);
    }
  }
  lines_ = wrapDescription(renderer_, raw, std::max(1, width));
}

void Description::render(const int recentIndex, const int x, const int y, const int width, const int height,
                         const bool background, const bool showLabel,
                         const HomeTheme::CarouselLabelColor labelColor,
                         const HomeTheme::CarouselShadowStyle shadowStyle, const bool showTitle,
                         const bool showAuthor, const bool showProgress, const bool showRating) const {
  if (width <= 0 || height <= 0) return;
  renderBackground(x, y, width, height, background);
  const ContentArea content = contentArea(y, height, showLabel);
  if (showLabel) renderLabel(x, y, "Book Details", labelColor);

  const auto& books = RECENT_BOOKS.getBooks();
  if (recentIndex < 0 || recentIndex >= static_cast<int>(books.size())) {
    renderer_.text.centered(systemFontId(), content.y + content.height / 2, "No recent");
    (void)shadowStyle;
    return;
  }

  const RecentBook& book = books[static_cast<size_t>(recentIndex)];
  constexpr int marginX = 20;
  constexpr int marginTop = 20;
  constexpr int titleFont = MONTSERRAT_16_FONT_ID;
  constexpr int authorFont = MONTSERRAT_12_FONT_ID;
  const int textWidth = std::max(1, width - marginX * 2);
  ensureLines(recentIndex, textWidth);
  int textBottom = content.y + marginTop;
  if (showTitle) {
    const std::string title = renderer_.text.truncate(titleFont, bookTitle(book).c_str(), textWidth,
                                                       EpdFontFamily::BOLD);
    renderer_.text.render(titleFont, x + marginX, textBottom, title.c_str(), true, EpdFontFamily::BOLD);
    textBottom += renderer_.text.getLineHeight(titleFont);
  }
  const int ratingStars = showRating ? cachedRatingStars_ : 0;
  const bool showAuthorText = showAuthor && !book.author.empty();
  if (showAuthorText || ratingStars > 0) {
    const int authorY = textBottom + 6;
    constexpr int iconSize = 24;
    constexpr int starGap = 2;
    const int authorLineHeight = renderer_.text.getLineHeight(authorFont);
    const int iconY = authorY + (authorLineHeight - iconSize) / 2;
    const int starsWidth = ratingStars > 0 ? ratingStars * iconSize + (ratingStars - 1) * starGap : 0;
    const int rightEdge = x + width - marginX;
    const int starsStart = rightEdge - starsWidth;
    const int authorWidth = std::max(0, (ratingStars > 0 ? starsStart - 8 : rightEdge) - (x + marginX));
    if (showAuthorText && authorWidth > 0) {
      const std::string author = renderer_.text.truncate(authorFont, book.author.c_str(), authorWidth,
                                                          EpdFontFamily::REGULAR);
      renderer_.text.renderGray(authorFont, x + marginX, authorY, author.c_str(), true, EpdFontFamily::REGULAR);
    }
    for (int star = 0; star < ratingStars; ++star) {
      renderer_.bitmap.icon(Star, starsStart + star * (iconSize + starGap), iconY, iconSize, iconSize);
    }
    textBottom = authorY + std::max(authorLineHeight, iconSize);
  }

  int descriptionY = textBottom + 20;
  if (showProgress) {
    const float progressPercent = book.progress * 100.0f;
    const int percentage = progressPercent < 0.0f
                               ? 0
                               : std::max(0, std::min(100, static_cast<int>(progressPercent + 0.5f)));
    const int barY = textBottom + 10;
    renderProgress(renderer_, x + marginX, barY, width, percentage);
    constexpr int descriptionGap = 40;
    descriptionY = barY + 5 + descriptionGap;
  }
  const int descriptionHeight = content.y + content.height - descriptionY;
  if (descriptionHeight <= 0 || lines_.empty()) {
    (void)shadowStyle;
    return;
  }

  const int font = MONTSERRAT_12_FONT_ID;
  const int lineHeight = renderer_.text.getLineHeight(font);
  const int visibleLines = std::max(1, descriptionHeight / std::max(1, lineHeight));
  const int lineCount = std::min(visibleLines, static_cast<int>(lines_.size()));
  int lineY = descriptionY;
  for (int index = 0; index < lineCount; ++index) {
    int textX = x + marginX;
    const DescriptionWidgetLine& line = lines_[static_cast<size_t>(index)];
    for (const DescriptionWidgetLine::Run& run : line.runs) {
      if (textX >= x + width - marginX) break;
      const int available = x + width - marginX - textX;
      const std::string shown = renderer_.text.truncate(font, run.text.c_str(), std::max(1, available), run.style);
      renderer_.text.render(font, textX, lineY, shown.c_str(), true, run.style);
      textX += renderer_.text.getWidth(font, shown.c_str(), run.style);
    }
    lineY += lineHeight;
  }
  (void)shadowStyle;
}
