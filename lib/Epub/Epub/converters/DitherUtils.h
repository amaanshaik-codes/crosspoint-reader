#pragma once

#include <stdint.h>
#include <string.h>

// 4x4 Bayer matrix for ordered dithering
inline const uint8_t bayer4x4[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

inline uint8_t quantize4LevelNearest(uint8_t gray) {
  uint8_t q = (gray + 42) / 85;
  return (q > 3) ? 3 : q;
}

// Maximum supported image width for diffusion buffers.
constexpr int kMaxErrorDiffusionWidth = 1024;
constexpr int kBlackClipThreshold = 26;
constexpr int kWhiteClipThreshold = 232;
constexpr int kFlatRegionDelta = 6;
constexpr int kStrongEdgeGradient = 30;

inline int clampInt(const int v, const int lo, const int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

inline int absInt(const int v) { return (v < 0) ? -v : v; }

// Stateful error-diffusion tracking (scanline based, left-to-right).
struct ErrorDiffusionState {
  int16_t* curr{nullptr};
  int16_t* next{nullptr};
  int width{0};
  int currentY{-32768};
  int lastX{-1};
  int runningMean{128};
  uint8_t* prevToneRow{nullptr};
  uint8_t* currToneRow{nullptr};
  bool hasPreviousRow{false};
  int prevAdjusted{0};
  bool hasPrev{false};
  bool enabled{false};
};

inline bool initErrorDiffusionState(ErrorDiffusionState& state, int16_t* currBuf, int16_t* nextBuf, int width) {
  state.curr = currBuf;
  state.next = nextBuf;
  state.width = width;
  state.currentY = -32768;
  state.lastX = -1;
  state.runningMean = 128;
  state.prevToneRow = nullptr;
  state.currToneRow = nullptr;
  state.hasPreviousRow = false;
  state.prevAdjusted = 0;
  state.hasPrev = false;
  state.enabled = false;

  if (!currBuf || !nextBuf || width <= 0 || width > kMaxErrorDiffusionWidth) {
    return false;
  }

  memset(currBuf, 0, sizeof(int16_t) * (width + 2));
  memset(nextBuf, 0, sizeof(int16_t) * (width + 2));
  state.enabled = true;
  return true;
}

inline void attachToneRows(ErrorDiffusionState& state, uint8_t* rowA, uint8_t* rowB) {
  if (!state.enabled || !rowA || !rowB) return;
  state.prevToneRow = rowA;
  state.currToneRow = rowB;
  memset(state.prevToneRow, 0, state.width);
  memset(state.currToneRow, 0, state.width);
  state.hasPreviousRow = false;
}

inline uint8_t applyEinkToneCurve(uint8_t gray) {
  // S-curve remap: deeper shadows, brighter highlights.
  int g = gray;
  g = (g * (g + 128)) >> 8;

  // Contrast stretch around midpoint for small text clarity.
  g = 128 + ((g - 128) * 164) / 128;
  if (g < 0) g = 0;
  if (g > 255) g = 255;
  return static_cast<uint8_t>(g);
}

inline bool prepareErrorDiffusionPixel(ErrorDiffusionState& state, int x, int y) {
  if (!state.enabled || !state.curr || !state.next) return false;
  if (x < 0 || x >= state.width) return false;

  if (y != state.currentY) {
    if (y == state.currentY + 1) {
      int16_t* tmp = state.curr;
      state.curr = state.next;
      state.next = tmp;
      memset(state.next, 0, sizeof(int16_t) * (state.width + 2));

      if (state.prevToneRow && state.currToneRow) {
        uint8_t* toneTmp = state.prevToneRow;
        state.prevToneRow = state.currToneRow;
        state.currToneRow = toneTmp;
        memset(state.currToneRow, 0, state.width);
        state.hasPreviousRow = true;
      }
    } else {
      memset(state.curr, 0, sizeof(int16_t) * (state.width + 2));
      memset(state.next, 0, sizeof(int16_t) * (state.width + 2));
      if (state.prevToneRow && state.currToneRow) {
        memset(state.prevToneRow, 0, state.width);
        memset(state.currToneRow, 0, state.width);
        state.hasPreviousRow = false;
      }
    }
    state.currentY = y;
    state.lastX = -1;
    state.hasPrev = false;
  }

  // Require monotonic left-to-right access for stable diffusion.
  if (x <= state.lastX) return false;
  state.lastX = x;
  return true;
}

// Detect if a pixel is in a flat region (used to disable dithering for solids).
// Flat regions (same or minimal variation) should not be dithered.
inline bool isNearFlatRegion(uint8_t gray) {
  // Simple heuristic: check if this pixel's rounded quantization level
  // is far from the dither range—if so, it's likely a flat area.
  // x/y are intentionally not part of this heuristic to keep it branch-light.
  uint8_t q = quantize4LevelNearest(gray);
  int paletteVal = q * 85;
  int delta = gray - paletteVal;
  if (delta < 0) delta = -delta;
  // Pixels near a palette color are effectively solid.
  return delta <= kFlatRegionDelta;
}

// Apply adaptive ordered dithering and quantize to 4 levels (0-3).
// Advanced strategy:
// 1) Contrast stretch (1.22x) around mid-gray for text sharpness.
// 2) Hard clip near-black (<=28) and near-white (>=227) to solid levels.
// 3) Disable dithering in flat regions to eliminate countable dots.
// 4) Selective dither only in gradient/transition zones for smooth detail.
inline uint8_t applyBayerDither4Level(uint8_t gray, int x, int y) {
  // Tone remap tuned for E-ink text and line art.
  int contrasted = applyEinkToneCurve(gray);
  if (contrasted < 0) contrasted = 0;
  if (contrasted > 255) contrasted = 255;

  // Hard clip extremes to preserve solid blacks/whites.
  if (contrasted <= kBlackClipThreshold) return 0;
  if (contrasted >= kWhiteClipThreshold) return 3;

  const uint8_t baseLevel = quantize4LevelNearest((uint8_t)contrasted);

  // If this pixel is in a flat color region, skip dithering entirely.
  if (isNearFlatRegion((uint8_t)contrasted)) {
    return baseLevel;
  }

  // Apply selective dithering in gradient/transition regions.
  const int bayer = bayer4x4[y & 3][x & 3];
  int strength = 2;
  // Boost dither slightly in mid-tone range for smoother gradients.
  if (contrasted >= 85 && contrasted <= 170) {
    strength = 3;
  }
  int adjusted = contrasted + (bayer - 7) * strength;

  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;
  return quantize4LevelNearest((uint8_t)adjusted);
}

// Non-dithered quantization with the same tone mapping and hard clipping.
inline uint8_t quantizeImage4LevelNoDither(uint8_t gray) {
  int toned = applyEinkToneCurve(gray);
  if (toned <= kBlackClipThreshold) return 0;
  if (toned >= kWhiteClipThreshold) return 3;
  return quantize4LevelNearest(static_cast<uint8_t>(toned));
}

// Floyd-Steinberg 4-level diffusion with damping to reduce worm artifacts.
inline uint8_t applyErrorDiffusion4Level(uint8_t gray, int localX, int y, ErrorDiffusionState& state) {
  if (!prepareErrorDiffusionPixel(state, localX, y)) {
    return applyBayerDither4Level(gray, localX, y);
  }

  int toned = applyEinkToneCurve(gray);

  // Update scene luminance estimate (IIR moving average) for adaptive clips.
  state.runningMean += (toned - state.runningMean) >> 5;

  int localMean = toned;
  int edge = 0;
  if (state.currToneRow && state.prevToneRow && localX < state.width) {
    const int left = (localX > 0) ? state.currToneRow[localX - 1] : toned;
    const int up = state.hasPreviousRow ? state.prevToneRow[localX] : toned;
    localMean = (toned * 2 + left + up) >> 2;
    edge = absInt(toned - left) + absInt(toned - up);

    // Local contrast enhancement: stronger on edges, softer on smooth tones.
    const int localDelta = toned - localMean;
    int boost = 0;
    if (edge >= kStrongEdgeGradient) {
      boost = localDelta >> 1;
    } else if (edge >= 12) {
      boost = localDelta / 3;
    } else {
      boost = localDelta / 6;
    }
    toned = clampInt(toned + boost, 0, 255);
  }

  // Adapt clips based on global scene brightness.
  const int sceneBias = clampInt((state.runningMean - 128) / 8, -8, 8);
  int blackClip = clampInt(kBlackClipThreshold + (sceneBias > 0 ? sceneBias : sceneBias / 2), 16, 40);
  int whiteClip = clampInt(kWhiteClipThreshold + (sceneBias < 0 ? sceneBias : sceneBias / 2), 216, 242);
  if (whiteClip - blackClip < 90) {
    const int mid = (blackClip + whiteClip) >> 1;
    blackClip = mid - 45;
    whiteClip = mid + 45;
  }

  if (state.currToneRow && localX < state.width) {
    state.currToneRow[localX] = static_cast<uint8_t>(toned);
  }

  if (toned <= blackClip) return 0;
  if (toned >= whiteClip) return 3;

  const int idx = localX + 1;
  int adjusted = toned + state.curr[idx];
  if (adjusted < 0) adjusted = 0;
  if (adjusted > 255) adjusted = 255;

  int gradient = 0;
  if (state.hasPrev) {
    gradient = adjusted - state.prevAdjusted;
    if (gradient < 0) gradient = -gradient;
  }
  if (edge > gradient) {
    gradient = edge;
  }

  uint8_t quantized = quantize4LevelNearest(static_cast<uint8_t>(adjusted));

  // Edge-aware bias for sharper small text and line art.
  if (gradient >= kStrongEdgeGradient) {
    if (adjusted <= 132 && quantized > 0) {
      quantized = static_cast<uint8_t>(quantized - 1);
    } else if (adjusted >= 162 && quantized < 3) {
      quantized = static_cast<uint8_t>(quantized + 1);
    }
  }

  state.prevAdjusted = adjusted;
  state.hasPrev = true;

  if (isNearFlatRegion(static_cast<uint8_t>(adjusted))) {
    return quantized;
  }

  const int quantizedValue = quantized * 85;
  int error = adjusted - quantizedValue;

  // Dampen error diffusion to avoid streaking/worm artifacts on e-ink.
  // Reduce diffusion further on strong edges to limit haloing.
  if (gradient >= kStrongEdgeGradient) {
    error >>= 1;
  } else {
    error = (error * 3) >> 2;
  }

  state.curr[idx + 1] += (error * 7) >> 4;
  state.next[idx - 1] += (error * 3) >> 4;
  state.next[idx] += (error * 5) >> 4;
  state.next[idx + 1] += error >> 4;

  return quantized;
}
