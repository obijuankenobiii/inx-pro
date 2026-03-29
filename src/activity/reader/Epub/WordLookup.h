#pragma once

#include <Epub/PageWordIndex.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

class EpubActivity;

/**
 * Shared page-word state for reader overlays.
 *
 * Dictionary and annotation both need the same word geometry, cursor movement,
 * framebuffer snapshot, and repaint recovery. Keeping it here prevents those
 * two overlays from diverging on touch or D-pad behaviour.
 */
class WordLookup {
 public:
  bool buildGeometry(EpubActivity& activity);
  void clear();

  bool empty() const { return words_.empty(); }
  std::vector<PageWordHit>& words() { return words_; }
  const std::vector<PageWordHit>& words() const { return words_; }
  std::vector<size_t>& lineFirst() { return lineFirst_; }
  const std::vector<size_t>& lineFirst() const { return lineFirst_; }
  size_t& focus() { return focus_; }
  size_t focus() const { return focus_; }

  bool focusAt(int x, int y);
  bool handleNavigation(EpubActivity& activity);

  bool captureFramebuffer(EpubActivity& activity);
  bool restoreFramebuffer(EpubActivity& activity) const;

 private:
  void moveFocusWord(int delta);
  void moveFocusLine(int delta);
  bool isDuplicateNavEdge(int direction, unsigned long now);

 static constexpr size_t kCaptureChunkBytes = 8000;

  struct CaptureBufferDeleter {
    void operator()(uint8_t* buffer) const;
  };
  using CaptureBuffer = std::unique_ptr<uint8_t, CaptureBufferDeleter>;

  static CaptureBuffer allocateCaptureBuffer(size_t bytes);

  std::vector<PageWordHit> words_;
  std::vector<size_t> lineFirst_;
  size_t focus_ = 0;

  // Selection overlays keep a full copy of the 480x800 1bpp framebuffer.
  // It is cold data read only when dismissing the overlay, so keep it out of
  // the internal heap used by the display and image decoders.
  std::vector<CaptureBuffer> captureChunks_;
  CaptureBuffer captureMonolithic_;
  bool captureUsesMonolithic_ = false;
  size_t captureBytes_ = 0;
  bool captureValid_ = false;

  unsigned long lastNavEdgeMs_ = 0;
  int lastNavEdgeDir_ = -1;
  int navRepeatDir_ = -1;
  unsigned long navRepeatNextMs_ = 0;
};
