#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <numeric>
#include <algorithm>  // std::upper_bound
#include <unordered_map>
#include <cstdint>    // uint8_t
#include <random>
#include "RandomUtils.h"

// Sentinel value stored in the grid and SideCell when a cell contains no symbol.
static constexpr uint8_t EMPTY_SYM = 0xFF;

struct Symbol {
    std::string name;
    int counter;
    double value; // For PAY symbols if needed

    Symbol() : name(""), counter(1), value(0.0) {}
    Symbol(const std::string& name, int counter = 1, double value = 0.0)
        : name(name), counter(counter), value(value) {
    }
};

class SymbolStructure {
private:
    std::vector<std::string> symbols;
    std::vector<std::vector<int>> paytable_vec;
    std::map<std::string, std::vector<int>> paytable;
    std::vector<int> scatterPrizes;
    std::unordered_map<std::string, std::vector<std::string>> wildSubstitutions;
    // Fast O(1) name → index lookup; built once in every constructor.
    std::unordered_map<std::string, int> symbolLookup;
    uint8_t wildId = EMPTY_SYM;  // precomputed ID of the wild symbol (0xFF = no wild)

public:
    SymbolStructure() = default;

    SymbolStructure(const std::vector<std::string>& symbolNames, const std::vector<std::vector<int>>& symbolPayouts) {
        for (size_t i = 0; i < symbolNames.size(); ++i) {
            symbols.push_back(symbolNames[i]);
            paytable_vec.push_back(symbolPayouts[i]);
            paytable[symbolNames[i]] = symbolPayouts[i];
            symbolLookup[symbolNames[i]] = static_cast<int>(i);
        }
    }

    SymbolStructure(const std::vector<std::string>& symbolNames,
        const std::vector<std::vector<int>>& symbolPayouts,
        const std::unordered_map<std::string, std::vector<std::string>>& wildSubs)
        : symbols(symbolNames), paytable_vec(symbolPayouts), wildSubstitutions(wildSubs) {
        for (size_t i = 0; i < symbolNames.size(); ++i) {
            paytable[symbolNames[i]] = symbolPayouts[i];
            symbolLookup[symbolNames[i]] = static_cast<int>(i);
        }
    }

    // Check if a symbol is wild and get its substitutions
    std::vector<std::string> getWildSubstitutions(const std::string& wildSymbol) const {
        auto it = wildSubstitutions.find(wildSymbol);
        if (it != wildSubstitutions.end()) {
            return it->second;
        }
        return {};
    }

    const std::vector<std::string>& getSymbols() const { return symbols; }
    const std::vector< std::vector<int>>& getPaytableVec() const { return paytable_vec; }
    const std::map<std::string, std::vector<int>>& getPaytable() const { return paytable; }
    const std::vector<int>& getScatterPrizes() const { return scatterPrizes; }
    const std::unordered_map<std::string, int>& getLookup() const { return symbolLookup; }
    uint8_t getWildId() const { return wildId; }

    // Call once after construction to register the wild symbol name.
    void setWild(const std::string& wildName) {
        auto it = symbolLookup.find(wildName);
        wildId = (it != symbolLookup.end()) ? static_cast<uint8_t>(it->second) : EMPTY_SYM;
    }

    // Additional functionality for SymbolStructure can go here
    // For example, a method to find a symbol by name and return its index or payouts
    int findSymbolIndex(const std::string& name) const {
        // O(1) hash lookup — was an O(n) linear scan called on every win result.
        auto it = symbolLookup.find(name);
        return (it != symbolLookup.end()) ? it->second : -1;
    }

    const std::vector<int>* findSymbolPayouts(const std::string& name) const {
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (symbols[i] == name) return &paytable_vec[i];
        }
        return nullptr; // Symbol not found
    }

    const int getNumSymbols() const {
        return symbols.size();
    }

    const int getWinLength() const {
        return paytable_vec[0].size();
    }
};

struct Reel {
    std::vector<std::string> symbols;
    std::vector<int> weights;
    std::vector<int> cumWeights;  // precomputed cumulative weights (only when weighted)
    int totalWeight = 0;          // precomputed total weight
    std::vector<uint8_t> symbolIds; // hot-path: integer IDs for every symbol on this reel strip

    // Constructor to accept a vector of strings
    Reel(const std::vector<std::string>& _symbols, const std::vector<int>& _weights = {})
        : symbols(_symbols), weights(_weights) {
        if (!_weights.empty()) {
            cumWeights.resize(_weights.size());
            int sum = 0;
            for (size_t i = 0; i < _weights.size(); ++i) {
                sum += _weights[i];
                cumWeights[i] = sum;
            }
            totalWeight = sum;
        }
    }

    bool isWeighted() const { return !weights.empty(); }

    // Populate symbolIds from the name→index lookup in SymbolStructure.
    // Must be called once before any hot-path spin code that reads symbolIds.
    void buildIds(const std::unordered_map<std::string, int>& lookup) {
        symbolIds.resize(symbols.size());
        for (size_t i = 0; i < symbols.size(); ++i) {
            auto it = lookup.find(symbols[i]);
            symbolIds[i] = (it != lookup.end()) ? static_cast<uint8_t>(it->second) : EMPTY_SYM;
        }
    }
};

class ReelSet {
private:
    std::string mask;

    // Optional over/under reels
    std::unique_ptr<Reel> overReel;
    std::unique_ptr<Reel> underReel;
    std::string overMask;
    std::string underMask;

public:
    std::vector<Reel> reels;
    std::vector<int> currentIndices; // Store current indices
    int currentOverIndex = 0;  // Store current over reel index
    int currentUnderIndex = 0; // Store current under reel index

    // Constructor for backward compatibility (no over/under reels)
    ReelSet(const std::vector<Reel>& reels, const std::string& mask)
        : reels(reels), mask(mask), currentIndices(reels.size(), 0) {
    }

    // Constructor with optional over/under reels
    ReelSet(const std::vector<Reel>& reels, const std::string& mask,
        const Reel* overReel, const std::string& overMask,
        const Reel* underReel, const std::string& underMask)
        : reels(reels), mask(mask), currentIndices(reels.size(), 0),
        overMask(overMask), underMask(underMask) {
        if (overReel) {
            this->overReel = std::make_unique<Reel>(*overReel);
        }
        if (underReel) {
            this->underReel = std::make_unique<Reel>(*underReel);
        }
    }

    // Default constructor
    ReelSet() {}

    // Copy constructor
    ReelSet(const ReelSet& other)
        : reels(other.reels), mask(other.mask), currentIndices(other.currentIndices),
        overMask(other.overMask), underMask(other.underMask),
        currentOverIndex(other.currentOverIndex), currentUnderIndex(other.currentUnderIndex) {
        if (other.overReel) {
            overReel = std::make_unique<Reel>(*other.overReel);
        }
        if (other.underReel) {
            underReel = std::make_unique<Reel>(*other.underReel);
        }
    }

    // Copy assignment operator
    ReelSet& operator=(const ReelSet& other) {
        if (this != &other) {
            reels = other.reels;
            mask = other.mask;
            currentIndices = other.currentIndices;
            overMask = other.overMask;
            underMask = other.underMask;
            currentOverIndex = other.currentOverIndex;
            currentUnderIndex = other.currentUnderIndex;

            if (other.overReel) {
                overReel = std::make_unique<Reel>(*other.overReel);
            }
            else {
                overReel.reset();
            }

            if (other.underReel) {
                underReel = std::make_unique<Reel>(*other.underReel);
            }
            else {
                underReel.reset();
            }
        }
        return *this;
    }

    // Move constructor
    ReelSet(ReelSet&& other) noexcept = default;

    // Move assignment operator
    ReelSet& operator=(ReelSet&& other) noexcept = default;

    // Build uint8_t symbolIds for every reel (and over/under reels) in this set.
    // Must be called once per ReelSet before any hot-path spin code.
    void buildSymbolIds(const std::unordered_map<std::string, int>& lookup) {
        for (auto& reel : reels) {
            reel.buildIds(lookup);
        }
        if (overReel)  overReel->buildIds(lookup);
        if (underReel) underReel->buildIds(lookup);
    }

    // Check if this reelset has over/under reels
    bool hasOverReel() const { return overReel != nullptr; }
    bool hasUnderReel() const { return underReel != nullptr; }

    // Get over/under reels
    const Reel* getOverReel() const { return overReel.get(); }
    const Reel* getUnderReel() const { return underReel.get(); }

    // Calculate complete cycle
    int getCycle() const {
        int cycle = 1;
        for (const auto& reel : reels) {
            cycle *= reel.isWeighted() ? std::accumulate(reel.weights.begin(), reel.weights.end(), 0) : reel.symbols.size();
        }
        return cycle;
    }

    // Spin reels method - now also spins over/under if they exist
    void spinReels() {
        // Write directly into currentIndices — no temporary vector, no copy.
        // currentIndices is already sized correctly from the constructor.
        for (int reelIndex = 0; reelIndex < (int)reels.size(); ++reelIndex) {
            const Reel& reel = reels[reelIndex];
            if (reel.isWeighted()) {
                // Use precomputed cumulative weights + binary search (was: getRandFromDist
                // which re-accumulated the total on every call and did a linear scan).
                int rnd = getRand(mask, reel.totalWeight);
                currentIndices[reelIndex] = static_cast<int>(
                    std::upper_bound(reel.cumWeights.begin(), reel.cumWeights.end(), rnd)
                    - reel.cumWeights.begin());
            }
            else {
                currentIndices[reelIndex] = getRand(mask, reel.symbols.size());
            }
        }

        // Spin over reel if it exists
        if (overReel) {
            if (overReel->isWeighted()) {
                int rnd = getRand(overMask, overReel->totalWeight);
                currentOverIndex = static_cast<int>(
                    std::upper_bound(overReel->cumWeights.begin(), overReel->cumWeights.end(), rnd)
                    - overReel->cumWeights.begin());
            }
            else {
                currentOverIndex = getRand(overMask, overReel->symbols.size());
            }
        }

        // Spin under reel if it exists
        if (underReel) {
            if (underReel->isWeighted()) {
                int rnd = getRand(underMask, underReel->totalWeight);
                currentUnderIndex = static_cast<int>(
                    std::upper_bound(underReel->cumWeights.begin(), underReel->cumWeights.end(), rnd)
                    - underReel->cumWeights.begin());
            }
            else {
                currentUnderIndex = getRand(underMask, underReel->symbols.size());
            }
        }
    }

    // Get current symbol from over/under reels
    std::string getCurrentOverSymbol() const {
        if (overReel && currentOverIndex < overReel->symbols.size()) {
            return overReel->symbols[currentOverIndex];
        }
        return "";
    }

    std::string getCurrentUnderSymbol() const {
        if (underReel && currentUnderIndex < underReel->symbols.size()) {
            return underReel->symbols[currentUnderIndex];
        }
        return "";
    }
};