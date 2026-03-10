#pragma once

#include <random>
#include <algorithm>

namespace npc {

class Random {
public:
    static Random& instance() {
        static Random rng;
        return rng;
    }

    void seed(unsigned int s) { engine_.seed(s); }

    // [0.0, 1.0)
    float unit() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(engine_); }

    // [min, max]
    int range(int min, int max) { return std::uniform_int_distribution<int>(min, max)(engine_); }

    // [min, max)
    float range(float min, float max) { return std::uniform_real_distribution<float>(min, max)(engine_); }

    bool chance(float probability) { return unit() < probability; }

    // Gaussian distribution
    float gaussian(float mean, float stddev) {
        return std::normal_distribution<float>(mean, stddev)(engine_);
    }

    // Pick random index
    size_t index(size_t size) {
        if (size == 0) return 0;
        return static_cast<size_t>(std::uniform_int_distribution<size_t>(0, size - 1)(engine_));
    }

private:
    Random() : engine_(std::random_device{}()) {}
    std::mt19937 engine_;
};

using RandomGenerator = Random;

} // namespace npc
