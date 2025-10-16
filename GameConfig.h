#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include "Symbols.h"
#include "PrizeDistribution.h"

// Forward declaration if Game-related classes need to be referenced
class Stats; // Forward declaration of Stats class

class GameConfig {
private:
    std::string filename;
    std::mutex config_mutex;

    // JSON object to hold the configuration data
    nlohmann::json config_json;

    // Parsed data
    std::vector<std::string> rtpHeaders;
    std::vector<std::string> featureNames;

public:
    GameConfig(const std::string& filename) : filename(filename) {
        std::lock_guard<std::mutex> lock(config_mutex);
        try {
            std::ifstream file(filename);
            if (!file.is_open()) {
                throw std::runtime_error("Failed to open " + filename);
            }
            file >> config_json;
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to load " << filename << ": " << e.what() << std::endl;
            throw; // Rethrow exception after logging
        }

        parseRTPHeaders();    // Parse RTP headers after loading the file
    }

    template<typename T>
    T parseVar(const std::string& key) {
        std::lock_guard<std::mutex> lock(config_mutex);
        if (!config_json.contains(key)) {
            throw std::invalid_argument("Key not found in configuration: " + key);
        }
        return config_json[key].get<T>();
    }

    template<typename T>
    std::vector<T> parseVec(const std::string& key, std::string subLevel = "") {
        if (!config_json.contains(key)) {
            throw std::invalid_argument("Key not found in configuration: " + key);
        }
        if (!subLevel.empty()) {
            return config_json[key][subLevel].get<std::vector<T>>();
        }
        else
            return config_json[key].get<std::vector<T>>();
    }

    template<typename T>
    std::vector<std::vector<T>> parseArray(const std::string& key) {
        std::lock_guard<std::mutex> lock(config_mutex);
        if (!config_json.contains(key)) {
            throw std::invalid_argument("Key not found in configuration: " + key);
        }
        return config_json[key].get<std::vector<std::vector<T>>>();
    }

    void parseRTPHeaders() {
        rtpHeaders = parseVec<std::string>("payHeaders");
    }

    const std::vector<std::string>& getRTPHeaders() const {
        return rtpHeaders;
    }

    std::vector<std::string> getGameInfo() {
        std::vector<std::string> gameInfo;
        gameInfo.push_back(parseVar<std::string>("gameName"));
        gameInfo.push_back(parseVar<std::string>("RTP"));
        gameInfo.push_back(parseVar<std::string>("gameMode"));
        return gameInfo;
    }

    SymbolStructure parseSymbolStructure() {
        std::vector<std::string> symbols;
        std::vector<std::vector<int>> paytable;
        std::vector<std::string> symbolNames = config_json["paytable"]["symbols"].get<std::vector<std::string>>();
        std::unordered_map<std::string, std::vector<std::string>> wildSubs;

        for (int i = 0; i < symbolNames.size(); i++) {
            symbols.push_back(symbolNames[i]);
            std::vector<int> payout = config_json["paytable"]["pays"][symbolNames[i]].get<std::vector<int>>();
            paytable.push_back(payout);
        }

        for (const auto& item : config_json["paytable"]["wildSubs"].items()) {
            std::string wildSymbol = item.key();
            std::vector<std::string> substitutes = item.value().get<std::vector<std::string>>();
            wildSubs[wildSymbol] = substitutes;
        }

        return SymbolStructure(symbols, paytable, wildSubs);
    }

    ReelSet parseReelSet(const std::string& reelSetName, std::string maskName = "") {
        auto& reelSetConfig = this->config_json["reel_sets"];
        for (auto& item : reelSetConfig) {
            if (item["name"] == reelSetName) {
                std::vector<Reel> reels;
                auto& reelsConfig = item["reels"];

                // Parse main reels
                for (auto& reelConfig : reelsConfig) {
                    std::vector<std::string> symbols = reelConfig["symbols"].get<std::vector<std::string>>();
                    std::vector<int> weights;
                    if (reelConfig.contains("weights")) {
                        weights = reelConfig["weights"].get<std::vector<int>>();
                    }
                    reels.push_back(Reel(symbols, weights));
                }

                std::string mask = maskName.empty() ? static_cast<std::string>(item["mask"]) : maskName;

                // Check for optional over/under reels
                Reel* overReel = nullptr;
                Reel* underReel = nullptr;
                std::string overMask = "";
                std::string underMask = "";

                if (item.contains("overReel")) {
                    auto& overConfig = item["overReel"];
                    std::vector<std::string> overSymbols = overConfig["symbols"].get<std::vector<std::string>>();
                    std::vector<int> overWeights;
                    if (overConfig.contains("weights")) {
                        overWeights = overConfig["weights"].get<std::vector<int>>();
                    }
                    overReel = new Reel(overSymbols, overWeights);
                    overMask = overConfig.contains("mask") ?
                        static_cast<std::string>(overConfig["mask"]) : mask + "_OVER";
                }

                if (item.contains("underReel")) {
                    auto& underConfig = item["underReel"];
                    std::vector<std::string> underSymbols = underConfig["symbols"].get<std::vector<std::string>>();
                    std::vector<int> underWeights;
                    if (underConfig.contains("weights")) {
                        underWeights = underConfig["weights"].get<std::vector<int>>();
                    }
                    underReel = new Reel(underSymbols, underWeights);
                    underMask = underConfig.contains("mask") ?
                        static_cast<std::string>(underConfig["mask"]) : mask + "_UNDER";
                }

                // Create ReelSet with optional over/under reels
                ReelSet result(reels, mask, overReel, overMask, underReel, underMask);

                // Clean up temporary pointers
                delete overReel;
                delete underReel;

                return result;
            }
        }
        throw std::invalid_argument("ReelSet not found: " + reelSetName);
    }

    std::unordered_map<std::string, ReelSet> parseAllReelSets() {
        std::unordered_map<std::string, ReelSet> reelSetsMap;
        for (auto& item : config_json["reel_sets"]) {
            std::string name = item["name"];
            ReelSet reelSet = parseReelSet(name);
            reelSetsMap[name] = reelSet;
        }
        return reelSetsMap;
    }

    // Get PrizeDistribution object from config
    template <typename PrizeType>
    PrizeDistribution<PrizeType> parsePrizeDistribution(const std::string& prizeDistName, std::string subLevel = "") {
        // Get the reference to the config JSON node, starting from the root
        json prizeDistConfig = this->config_json;

        // If subLevel is provided, navigate to the specific sublevel
        if (!subLevel.empty()) {
            std::istringstream subLevelStream(subLevel);
            std::string level;
            while (getline(subLevelStream, level, '/')) {
                prizeDistConfig = prizeDistConfig[level];
            }
        }
        prizeDistConfig = prizeDistConfig[prizeDistName];

        std::string mask = prizeDistConfig["mask"];
        std::vector<PrizeType> prizes = prizeDistConfig["prizes"].get<std::vector<PrizeType>>();
        // Check if weights exist, default to 1s if not
        std::vector<int> weights;
        if (prizeDistConfig.contains("weights")) {
            weights = prizeDistConfig["weights"].get<std::vector<int>>();
        }
        else {
            weights = std::vector<int>(prizes.size(), 1); // Default weights to 1 for each prize
        }
        return PrizeDistribution<PrizeType>(mask, prizes, weights);
    }

    // Get vector of PrizeDistribution objects from config
    template <typename PrizeType>
    std::vector<PrizeDistribution<PrizeType>> parsePDVec(const std::string& prizeDistName) {
        std::vector<PrizeDistribution<PrizeType>> prizeDists;
        json& prizeDistConfig = this->config_json[prizeDistName];
        for (auto& item : prizeDistConfig.items()) {
            std::string key = item.key();
            prizeDists.push_back(parsePrizeDistribution<PrizeType>(key, prizeDistName));
        }
        return prizeDists;
    }
};