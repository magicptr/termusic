// Particles: audio-driven rectangular emitters.
//
// A particle is born from a frequency column that has energy, rises, and dies.
// The pool is fixed: emission only ever reuses a dead slot, so a loud passage
// cannot allocate, and silence stops spawning and lets the last particles fall
// out of existence. No starfield, no random scatter for its own sake.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <vector>

#include "ui/visualizer/renderer.hpp"

namespace termusic::ui {
namespace {

using namespace ftxui;

/// The particle: the same small rectangle the other two styles use.
constexpr std::string_view kParticle = "\u25aa";

/// Hard limits. Both are compile-time constants so the pool can never grow:
/// `kCapacity` bounds the memory, `kMaxPerFrame` bounds the work.
constexpr int kCapacity = 192;
constexpr int kMaxPerFrame = 24;

/// Emission. A column emits when its band is above the threshold, at a rate
/// proportional to how far above it is, so quiet music is sparse and loud music
/// is dense without any global gain.
constexpr float kSpawnThreshold = 0.22F;
constexpr float kSpawnsPerSecond = 26.0F;
/// A beat multiplies the emission for a moment: the burst is the audible hit.
constexpr float kBeatBurst = 2.2F;

/// Motion, in grid cells per second. Upward only: the vertical direction is the
/// style's identity, so it is never randomised away.
constexpr float kRiseMin = 7.0F;
constexpr float kRiseMax = 16.0F;
constexpr float kLifetimeMin = 0.55F;
constexpr float kLifetimeMax = 1.60F;
/// Sideways drift is a small fraction of the rise: enough to avoid a rigid
/// column, small enough to keep the frequency axis readable.
constexpr float kDriftMax = 1.6F;

struct Particle {
  float x = 0.0F;         // grid column, sub-cell
  float y = 0.0F;         // grid row, sub-cell (0 = top)
  float rise = 0.0F;      // cells per second, upward (negative y)
  float drift = 0.0F;     // cells per second, sideways
  float life = 0.0F;      // seconds left
  float lifetime = 1.0F;  // seconds it started with
  float intensity = 0.0F; // colour source
  bool alive = false;
};

/// Deterministic per-particle variation: a counter hash, so the style has no
/// RNG state to leak between frames and the same music draws the same shapes.
float hash01(std::uint32_t value) {
  value ^= value >> 16;
  value *= 0x7feb352dU;
  value ^= value >> 15;
  value *= 0x846ca68bU;
  value ^= value >> 16;
  return static_cast<float>(value & 0xFFFFFFU) /
         static_cast<float>(0x1000000U);
}

class ParticlesRenderer final : public VisualizerRenderer {
public:
  std::string_view id() const override { return "particles"; }

  void reset() override {
    pool_.assign(kCapacity, Particle{});
    columns_ = 0;
    rows_ = 0;
    counter_ = 0;
    drawn_ = 0;
    alive_ = 0;
  }

  void update(const VisualizerFrame &frame) override {
    if (pool_.empty())
      reset();
    if (frame.columns != columns_ || frame.rows != rows_) {
      // A resize re-homes the particle field instead of leaving particles at
      // coordinates that no longer exist.
      columns_ = frame.columns;
      rows_ = frame.rows;
      for (Particle &particle : pool_)
        particle.alive = false;
    }
    if (columns_ <= 0 || rows_ <= 0)
      return;
    const float step = std::clamp(frame.dt, 0.0F, 0.25F);
    for (Particle &particle : pool_) {
      if (!particle.alive)
        continue;
      particle.life -= step;
      particle.y += particle.rise * step;
      particle.x += particle.drift * step;
      const bool off_screen = particle.y < -1.0F ||
                              particle.x < -1.0F ||
                              particle.x > static_cast<float>(columns_);
      if (particle.life <= 0.0F || off_screen)
        particle.alive = false;
    }
    if (frame.live)
      emit(frame, step);
    alive_ = 0;
    for (const Particle &particle : pool_) {
      if (particle.alive)
        ++alive_;
    }
  }

  Element render(const VisualizerFrame &frame) override {
    grid_.resize(frame.columns, frame.rows);
    grid_.clear();
    drawn_ = 0;
    for (const Particle &particle : pool_) {
      if (!particle.alive)
        continue;
      const int x = static_cast<int>(std::lround(particle.x));
      const int y = static_cast<int>(std::lround(particle.y));
      if (x < 0 || y < 0 || x >= frame.columns || y >= frame.rows)
        continue;
      // Fading with age makes a particle's death read as "it rose away"
      // rather than "it was switched off".
      const float age = 1.0F - std::clamp(particle.life / particle.lifetime,
                                          0.0F, 1.0F);
      const float intensity =
          std::clamp(particle.intensity * (1.0F - 0.55F * age), 0.0F, 1.0F);
      if (intensity < 0.05F)
        continue;
      if (grid_.filled(x, y))
        continue; // one cell, one particle: the newest keeps it
      grid_.put(x, y, kParticle, intensity);
      ++drawn_;
    }
    return grid_.toElement(frame);
  }

  VisualizerStats stats() const override {
    VisualizerStats stats;
    stats.capacity = kCapacity;
    stats.retained = alive_;
    stats.drawn = drawn_;
    return stats;
  }

private:
  void emit(const VisualizerFrame &frame, float step) {
    const int bands = static_cast<int>(frame.bands.size());
    if (bands <= 0 || columns_ <= 0)
      return;
    const float burst = 1.0F + kBeatBurst * std::clamp(frame.beat, 0.0F, 1.0F);
    int spawned = 0;
    for (int column = 0; column < columns_ && spawned < kMaxPerFrame;
         ++column) {
      // The particle's x IS its frequency: the column maps onto the spectrum
      // exactly as the other styles map their cells.
      const double position =
          columns_ <= 1
              ? 0.0
              : static_cast<double>(column) *
                    static_cast<double>(bands - 1) /
                    static_cast<double>(columns_ - 1);
      const auto at = static_cast<std::size_t>(position);
      const float energy = std::clamp(frame.bands[at], 0.0F, 1.0F);
      if (energy <= kSpawnThreshold)
        continue;
      const float excess = (energy - kSpawnThreshold) /
                           (1.0F - kSpawnThreshold);
      const float expected =
          excess * kSpawnsPerSecond * burst * step;
      // Deterministic fractional emission: the integer part always spawns, the
      // fraction spawns when the hash says so. No accumulated RNG state.
      int count = static_cast<int>(expected);
      const float fraction = expected - static_cast<float>(count);
      if (hash01(counter_++ * 2654435761U + static_cast<std::uint32_t>(column)) <
          fraction)
        ++count;
      for (int index = 0; index < count && spawned < kMaxPerFrame; ++index) {
        Particle *slot = freeSlot();
        if (slot == nullptr)
          return; // pool exhausted: the picture thins out, it never grows
        const float jitter = hash01(counter_++ * 2246822519U);
        const float speed = hash01(counter_++ * 3266489917U);
        slot->alive = true;
        slot->x = static_cast<float>(column) + (jitter - 0.5F) * 0.6F;
        slot->y = static_cast<float>(rows_) - 1.0F;
        slot->rise = -(kRiseMin + (kRiseMax - kRiseMin) * speed);
        slot->lifetime =
            kLifetimeMin + (kLifetimeMax - kLifetimeMin) * jitter;
        slot->life = slot->lifetime;
        slot->drift = (hash01(counter_++ * 668265263U) - 0.5F) * 2.0F *
                      kDriftMax;
        slot->intensity = std::clamp(0.35F + 0.65F * energy, 0.0F, 1.0F);
        ++spawned;
      }
    }
  }

  /// The first dead slot, or nullptr. The pool is never resized.
  Particle *freeSlot() {
    for (Particle &particle : pool_) {
      if (!particle.alive)
        return &particle;
    }
    return nullptr;
  }

  int columns_ = 0;
  int rows_ = 0;
  int drawn_ = 0;
  int alive_ = 0;
  std::uint32_t counter_ = 0;
  std::vector<Particle> pool_;
  CellGrid grid_;
};

} // namespace

std::unique_ptr<VisualizerRenderer> makeParticlesRenderer() {
  return std::make_unique<ParticlesRenderer>();
}

} // namespace termusic::ui
