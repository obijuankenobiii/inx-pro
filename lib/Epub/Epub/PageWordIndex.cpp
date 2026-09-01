/**
 * @file PageWordIndex.cpp
 */

#include "Epub/PageWordIndex.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>

namespace {

constexpr uint8_t scriptScale = 70;

EpdFontFamily::Style bionicStyle(const EpdFontFamily::Style style) {
  switch (style) {
    case EpdFontFamily::ITALIC:
      return EpdFontFamily::BOLD_ITALIC;
    case EpdFontFamily::REGULAR:
      return EpdFontFamily::BOLD;
    case EpdFontFamily::BOLD:
    case EpdFontFamily::BOLD_ITALIC:
    default:
      return style;
  }
}

int segmentWidth(const GfxRenderer& renderer, const int fontId, const std::string& text,
                 const EpdFontFamily::Style style, const bool smallCaps, const uint8_t verticalAlign) {
  if (verticalAlign == TextBlock::SUPERSCRIPT || verticalAlign == TextBlock::SUBSCRIPT) {
    return renderer.text.getScaledWidth(fontId, text.c_str(), scriptScale, style);
  }
  return smallCaps ? renderer.text.getSmallCapsWidth(fontId, text.c_str(), style)
                   : renderer.text.getWidth(fontId, text.c_str(), style);
}

int wordWidth(const GfxRenderer& renderer, const int fontId, const std::string& text,
              const EpdFontFamily::Style style, const uint8_t bionicPrefix, const bool smallCaps,
              const uint8_t verticalAlign) {
  if (bionicPrefix == 0 || bionicPrefix >= text.size()) {
    return segmentWidth(renderer, fontId, text, style, smallCaps, verticalAlign);
  }
  return segmentWidth(renderer, fontId, text.substr(0, bionicPrefix), bionicStyle(style), smallCaps, verticalAlign) +
         segmentWidth(renderer, fontId, text.substr(bionicPrefix), style, smallCaps, verticalAlign);
}

void setWordGeometry(PageWordHit& hit, const GfxRenderer& renderer, const int fontId, const int baseY,
                     const std::string& text, const EpdFontFamily::Style style, const uint8_t bionicPrefix,
                     const bool smallCaps, const uint8_t verticalAlign) {
  const int lineHeight = renderer.text.getLineHeight(fontId);
  hit.screenY = baseY;
  hit.screenH = lineHeight;
  if (verticalAlign == TextBlock::SUPERSCRIPT) {
    hit.screenY -= std::max(1, lineHeight / 3);
    hit.screenH = std::max(1, lineHeight * scriptScale / 100);
  } else if (verticalAlign == TextBlock::SUBSCRIPT) {
    hit.screenY += std::max(1, lineHeight / 5);
    hit.screenH = std::max(1, lineHeight * scriptScale / 100);
  }
  hit.screenW = std::max(1, wordWidth(renderer, fontId, text, style, bionicPrefix, smallCaps, verticalAlign));
}

}

void buildPageWordIndex(const Page& page, GfxRenderer& renderer, const int bodyFontId, const int headerFontId,
                        const int marginLeft, const int marginTop, std::vector<PageWordHit>& out,
                        std::vector<size_t>* lineStartsOut, const bool omitStoredWordStrings) {
  out.clear();
  if (lineStartsOut) {
    lineStartsOut->clear();
  }

  for (size_t ei = 0; ei < page.elements.size(); ++ei) {
    const auto& el = page.elements[ei];
    switch (el->getTag()) {
      case TAG_PageSmallCaps:
      case TAG_PageLine: {
        const TextBlock* tbPtr = nullptr;
        int16_t elemX = 0;
        int16_t elemY = 0;
        if (el->getTag() == TAG_PageSmallCaps) {
          const auto* sc = static_cast<const PageSmallCaps*>(el.get());
          tbPtr = &sc->getTextBlock();
          elemX = sc->xPos;
          elemY = sc->yPos;
        } else {
          const auto* pl = static_cast<const PageLine*>(el.get());
          tbPtr = &pl->getTextBlock();
          elemX = pl->xPos;
          elemY = pl->yPos;
        }
        const TextBlock& tb = *tbPtr;
        if (lineStartsOut) {
          lineStartsOut->push_back(out.size());
        }
        const int baseX = elemX + marginLeft;
        const int baseY = elemY + marginTop;
        tb.forEachWord([&](const size_t wi, const std::string& wtext, const int16_t relX,
                           const EpdFontFamily::Style st, const std::string& footnoteTarget) {
          PageWordHit h;
          h.elementIndex = ei;
          h.wordIndexInElement = wi;
          h.fontId = bodyFontId;
          if (!omitStoredWordStrings) {
            h.text = wtext;
          }
          h.screenX = baseX + relX;
          setWordGeometry(h, renderer, bodyFontId, baseY, wtext, st, tb.getBionicPrefixBytesAt(wi),
                          tb.isWordSmallCapsAt(wi), tb.getWordVerticalAlignAt(wi));
          h.isDropCap = false;
          h.footnoteTarget = footnoteTarget;
          out.push_back(std::move(h));
        });
        break;
      }
      case TAG_PageHeader: {
        const auto* ph = static_cast<const PageHeader*>(el.get());
        const TextBlock& tb = ph->getTextBlock();
        if (lineStartsOut) {
          lineStartsOut->push_back(out.size());
        }
        const int hdrFont = ph->getHeaderFontId();
        const int baseX = ph->xPos + marginLeft;
        const int baseY = ph->yPos + marginTop;
        tb.forEachWord([&](const size_t wi, const std::string& wtext, const int16_t relX,
                           const EpdFontFamily::Style st, const std::string& footnoteTarget) {
          PageWordHit h;
          h.elementIndex = ei;
          h.wordIndexInElement = wi;
          h.fontId = hdrFont;
          if (!omitStoredWordStrings) {
            h.text = wtext;
          }
          h.screenX = baseX + relX;
          setWordGeometry(h, renderer, hdrFont, baseY, wtext, st, tb.getBionicPrefixBytesAt(wi),
                          tb.isWordSmallCapsAt(wi), tb.getWordVerticalAlignAt(wi));
          h.isDropCap = false;
          h.footnoteTarget = footnoteTarget;
          out.push_back(std::move(h));
        });
        break;
      }
      case TAG_PageDropCap: {
        const auto* dc = static_cast<const PageDropCap*>(el.get());
        if (lineStartsOut) {
          lineStartsOut->push_back(out.size());
        }
        PageWordHit h;
        h.elementIndex = ei;
        h.wordIndexInElement = 0;
        const int df = dc->getDropCapFontId();
        h.fontId = df;
        {
          const std::string dct = dc->getDropCapText();
          if (!omitStoredWordStrings) {
            h.text = dct;
          }
          h.screenX = dc->xPos + marginLeft;
          if (dc->isInlineFirstLine()) {
            h.screenY = dc->yPos + marginTop + renderer.text.getFontAscenderSize(bodyFontId) -
                        renderer.text.getFontAscenderSize(df);
          } else {
            h.screenY = dc->yPos + marginTop + PageDropCap::VERTICAL_ADJUSTMENT;
          }
        h.screenW = std::max(1, renderer.text.getWidth(df, dct.c_str(), dc->getStyle()));
        }
        h.screenH = renderer.text.getLineHeight(df);
        h.isDropCap = true;
        out.push_back(std::move(h));
        break;
      }
      default:
        break;
    }
  }
}
