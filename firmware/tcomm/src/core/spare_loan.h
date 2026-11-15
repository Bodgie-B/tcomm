// Who has the interface's one spare picture (ui.cpp, Spare: the sky's size, 768,000 bytes of PSRAM)
// at any moment. Plain C++, so the host tests walk every path (tests/test_core.cpp, test_spare_loan).
//
// The memory audit of 2026-11-12 (recommendation 1): while the screensaver runs, the spare sits idle
// and the scene took another 768,000 bytes of PSRAM for its layer cache. Now the spare is lent to it.
//
// Ownership rules:
// - The interface owns the block. It is Free, or held by one art slot (a picture being drawn into it
//   by the drawing task, or a tile's fade showing it), or Lent to the screensaver as its scene cache.
//   Never two of these at once.
// - An art slot claims it only while it is Free and lets it go with release(). release() never takes
//   it from the screensaver.
// - The screensaver borrows it with lend() only while it is Free (nothing is drawing into it and no
//   fade shows it), only for exactly the block's size, 4-byte aligned, and only once: a second lend()
//   while it is out is refused. When lend() refuses, the screensaver allocates its own cache, as it
//   did before.
// - While it is Lent, every art claim is refused (and counted): each art slot simply asks again on a
//   later frame, so sky, tile, outside and doorbell change-overs wait until the screensaver stops.
// - It comes back exactly once, when the screensaver's core frees its cache (appStop, whatever ended
//   the run, or an appStart that failed after taking it): give_back() with the same pointer. Any
//   other pointer isn't the block; give_back() says false and the caller frees it as its own.
// - After every stop and every failed start the host calls reclaim(): the block must be home by then.
//   If it isn't (a bug), reclaim() takes it home and says so, so the art can never wait forever.
// - All of it runs in one task (the LVGL task, or a serial command holding the LVGL lock), so there
//   are no locks here. The drawing task only ever sees canvases an art slot has claimed.
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace tc {

class SpareLoan {
 public:
  static constexpr int kFree = -1;
  static constexpr int kLent = -2;  // the screensaver's scene cache

  int owner() const { return owner_; }  // an art slot (0 or more), kFree or kLent
  bool lent() const { return owner_ == kLent; }

  // An art slot takes it for a drawing. False while anyone else has it; while the screensaver has it
  // the refusal is counted (the slot asks again later).
  bool claim(int slot) {
    if (slot < 0) return false;
    if (owner_ == kLent) {
      if (refused_ != UINT32_MAX) refused_++;
      return false;
    }
    if (owner_ != kFree) return false;
    owner_ = slot;
    return true;
  }

  // The art slot is done with it. Does nothing (false) unless an art slot has it.
  bool release() {
    if (owner_ < 0) return false;
    owner_ = kFree;
    return true;
  }

  // The screensaver asks for `want` bytes for its scene cache. The block, if it is Free, exactly that
  // big and 4-byte aligned; else nullptr, and nothing changes.
  void *lend(void *block, size_t block_bytes, size_t want) {
    if (owner_ != kFree || !block || want != block_bytes || ((uintptr_t)block & 3u) != 0) return nullptr;
    owner_ = kLent;
    out_ = block;
    refused_ = 0;
    lends_++;
    return block;
  }

  // The screensaver frees p. True when p is the lent block: it is Free again. False (nothing
  // changes): p isn't ours, and the caller frees it itself.
  bool give_back(const void *p) {
    if (owner_ != kLent || !p || p != out_) return false;
    owner_ = kFree;
    out_ = nullptr;
    returns_++;
    return true;
  }

  // After the screensaver has stopped (or failed to start): true if the block was still out, which
  // is a bug; it is Free again either way.
  bool reclaim() {
    if (owner_ != kLent) return false;
    owner_ = kFree;
    out_ = nullptr;
    reclaims_++;
    return true;
  }

  uint32_t refused() const { return refused_; }  // art claims refused during the current or last loan
  uint32_t lends() const { return lends_; }
  uint32_t returns() const { return returns_; }
  uint32_t reclaims() const { return reclaims_; }

 private:
  int owner_ = kFree;
  const void *out_ = nullptr;
  uint32_t refused_ = 0, lends_ = 0, returns_ = 0, reclaims_ = 0;
};

}  // namespace tc
