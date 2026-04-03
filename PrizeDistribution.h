#pragma once

#include <string>
#include <vector>
#include <algorithm>  // std::upper_bound
#include <numeric>    // std::partial_sum
#include "RandomUtils.h"

// Define a template class to handle the prize distribution
template <typename PrizeType>
class PrizeDistribution {
private:
    std::string maskName;
    std::vector<PrizeType> prizes;
    std::vector<int> weights;
    std::vector<int> cumWeights;  // precomputed cumulative weights
    int total = 0;                // precomputed total weight

    // Recompute cumulative weights whenever the weight vector changes.
    void recompute() {
        cumWeights.resize(weights.size());
        int sum = 0;
        for (size_t i = 0; i < weights.size(); ++i) {
            sum += weights[i];
            cumWeights[i] = sum;
        }
        total = sum;
    }

public:
    // Default constructor
    PrizeDistribution() {}

    // Constructor with parameters
    PrizeDistribution(const std::string& mask, const std::vector<PrizeType>& prizeList, const std::vector<int>& weightList)
        : maskName(mask), prizes(prizeList), weights(weightList) {
        recompute();
    }

    // O(log n) lookup using precomputed cumulative weights.
    // Eliminates the two O(n) passes (accumulate + linear scan) that existed before.
    PrizeType getRandomPrize() const {
        int rnd = getRand(maskName, total);
        int idx = static_cast<int>(
            std::upper_bound(cumWeights.begin(), cumWeights.end(), rnd) - cumWeights.begin());
        return prizes[idx];
    }

    // Change Prizes
    void setPrizes(const std::vector<PrizeType>& newPrizes) { prizes = newPrizes; }
    // Change Prizes by index
    void setPrize(int index, const PrizeType& newPrize) { prizes[index] = newPrize; }

    // Change Weights — recompute cumulative weights afterwards
    void setWeights(const std::vector<int>& newWeights) { weights = newWeights; recompute(); }
    // Change Weights by index — recompute cumulative weights afterwards
    void setWeight(int index, int newWeight) { weights[index] = newWeight; recompute(); }

    // Getters
    const std::vector<PrizeType>& getPrizes() const { return prizes; }
    const std::vector<int>& getWeights() const { return weights; }
};

