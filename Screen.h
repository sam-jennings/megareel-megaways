#pragma once

#include <string>
#include <vector>
#include <iostream>
#include "RandomLogGenerator.h"
#include "Symbols.h"



using namespace std;
using json = nlohmann::json;

struct SideCell {
    uint8_t symbolId = EMPTY_SYM;
    int boostLevel = 0;   // 0=none, 1=regular boost (+1), 2=superboost
    int superMult = 0;    // Pre-assigned multiplier for superboost symbols (0 = not yet assigned)
};

class Screen {
private:
    int numReels;
    int maxHeight;
    std::vector<int> heights;
    vector<vector<uint8_t>> grid;
    // For over/under reels
    static constexpr int SIDE_LEN = 4;          // middle-four reels
    std::array<SideCell, SIDE_LEN> overRow{};
    std::array<SideCell, SIDE_LEN> underRow{};

    // Cached for display/logging only — NOT used in the hot simulation path.
    const std::vector<std::string>* symbolNames = nullptr;
    uint8_t wildId = EMPTY_SYM;

    // Helper: convert a symbol ID to its name string (display/logging only).
    const std::string& idToName(uint8_t id) const {
        static const std::string empty   = "";
        static const std::string unknown = "??";
        if (id == EMPTY_SYM) return empty;
        if (symbolNames && id < static_cast<uint8_t>(symbolNames->size()))
            return (*symbolNames)[id];
        return unknown;
    }

public:
    std::vector<std::pair<int, int>> markedPositions;

    // Default constructor
    Screen() {}

    // Constructor for screen with equal rows
    Screen(int _numReels, int _numRows) : numReels(_numReels), maxHeight(_numRows) {
        heights.reserve(_numReels);
        for (int i = 0; i < _numReels; ++i) {
            heights.push_back(_numRows);
        }
        resize(heights);
    }

    // Constructor for screen with variable heights
    Screen(const std::vector<int>& _heights) : numReels(_heights.size()), heights(_heights) {
        resize(heights);
    }

    // Call once after construction (from GameInstance::initializeGame) to register
    // the symbol name table and wild ID needed for display() / toJson().
    void init(const std::vector<std::string>& names, uint8_t wId) {
        symbolNames = &names;
        wildId = wId;
    }

    // For over/under reels
    inline bool middleReel(int reel) const { return reel >= 1 && reel <= 4; }

    // Hot-path match: compare uint8_t IDs.
    inline bool match(uint8_t id, uint8_t targetId, bool includeWild = true) const {
        if (id == EMPTY_SYM) return false;
        return (id == targetId) || (includeWild && id == wildId);
    }

    void setSideSymbol(bool over, int idx, uint8_t id, int boostLevel = 0, int superMult = 0) {
        auto& cell = (over ? overRow : underRow)[idx];
        cell.symbolId  = id;
        cell.boostLevel = boostLevel;
        cell.superMult  = superMult;
    }

    uint8_t getSideSymbol(bool over, int idx) const {
        return (over ? overRow : underRow)[idx].symbolId;
    }

    int getSideBoostLevel(bool over, int idx) const {
        return (over ? overRow : underRow)[idx].boostLevel;
    }

    void setSideBoosted(bool over, int idx, int level) {
        (over ? overRow : underRow)[idx].boostLevel = level;
    }

    int getSideMultiplier(bool over, int idx) const {
        return (over ? overRow : underRow)[idx].superMult;
    }

    // Call this after addSideSymbols() and after each cascadeSideRowIntegrated()
    void assignSuperboostMultipliers(std::function<int()> rollFn) {
        for (auto& cell : overRow)
            if (cell.boostLevel == 2 && cell.superMult == 0 && cell.symbolId != EMPTY_SYM)
                cell.superMult = rollFn();
        for (auto& cell : underRow)
            if (cell.boostLevel == 2 && cell.superMult == 0 && cell.symbolId != EMPTY_SYM)
                cell.superMult = rollFn();
    }

    // Resize the screen based on fixed number of rows
    void resize(int _numReels, int _numRows) {
        numReels  = _numReels;
        maxHeight = _numRows;
        heights.resize(numReels, _numRows);
        grid.resize(numReels, vector<uint8_t>(_numRows, EMPTY_SYM));
    }

    // Resize the screen with variable heights
    void resize(std::vector<int> newH) {
        heights   = newH;
        numReels  = heights.size();
        grid.resize(numReels);
        maxHeight = 0;
        for (int i = 0; i < numReels; ++i) {
            if (heights[i] > maxHeight) maxHeight = heights[i];
            grid[i].resize(heights[i], EMPTY_SYM);
        }
    }

    void setReelHeight(int r, int h) { heights[r] = h; grid[r].resize(h, EMPTY_SYM); }
    int  getReelHeight(int r) const  { return heights[r]; }

    void display(bool displayMarkedPositions = false) {
        cout << "Current Screen:" << endl;
        for (int i = 0; i < maxHeight; ++i) {
            for (int j = 0; j < numReels; ++j) {
                if (i >= heights[j]) { cout << setw(5) << "     "; continue; }
                const std::string& sym = idToName(grid[j][i]);
                if (displayMarkedPositions) {
                    bool marked = false;
                    for (const auto& pos : markedPositions)
                        if (pos.first == j && pos.second == i) { marked = true; break; }
                    if (marked) cout << setw(5) << "[" << sym << "] ";
                    else        cout << setw(5) << sym << "  ";
                } else {
                    cout << setw(5) << sym << "  ";
                }
            }
            cout << endl;
        }
    }

    // Update a single cell with a symbol ID
    void updateCell(int reel, int row, uint8_t id) {
        if (row >= 0 && row < heights[reel] && reel >= 0 && reel < numReels)
            grid[reel][row] = id;
    }

    // Function to clear the screen
    void clearScreen() {
        for (int i = 0; i < numReels; ++i)
            for (int j = 0; j < heights[i]; ++j)
                grid[i][j] = EMPTY_SYM;
    }

    // Generate the screen from a ReelSet using precomputed uint8_t symbolIds.
    void generateScreen(ReelSet& reelSet) {
        clearScreen();
        for (int reelIndex = 0; reelIndex < numReels; ++reelIndex) {
            const auto& reel     = reelSet.reels[reelIndex];
            const int   stripLen = static_cast<int>(reel.symbolIds.size());
            for (int rowIndex = 0; rowIndex < heights[reelIndex]; ++rowIndex) {
                int idx = (reelSet.currentIndices[reelIndex] + rowIndex) % stripLen;
                grid[reelIndex][rowIndex] = reel.symbolIds[idx];
            }
        }
    }

    // Count how many times a symbol ID appears on one reel (including side rows for middle reels).
    int countSymbolOnReel(int reelIndex, uint8_t id, bool includeWild = true) const {
        if (reelIndex < 0 || reelIndex >= numReels) return 0;
        int count = 0;
        for (int row = 0; row < heights[reelIndex]; ++row)
            if (match(grid[reelIndex][row], id, includeWild)) ++count;
        if (middleReel(reelIndex)) {
            if (match(overRow [reelIndex - 1].symbolId, id, includeWild)) ++count;
            if (match(underRow[reelIndex - 1].symbolId, id, includeWild)) ++count;
        }
        return count;
    }

    // Count how many times a symbol ID appears across the whole screen.
    int countSymbolOnScreen(uint8_t id, bool includeWild = true) const {
        int count = 0;
        for (int i = 0; i < numReels; ++i)
            count += countSymbolOnReel(i, id, includeWild);
        return count;
    }

    // Return (winLength, ways) for a given symbol ID.
    pair<int, int> getWaysForSymbol(uint8_t id) const {
        int length = 0, ways = 1;
        for (int i = 0; i < numReels; ++i) {
            int count = countSymbolOnReel(i, id);
            if (count > 0) { length++; ways *= count; }
            else break;
        }
        if (length == 0) ways = 0;
        return make_pair(length, ways);
    }


    json toJson(bool includeOver = false, bool includeUnder = false) const {
        json screenJson;

        if (includeOver) {
            json overJson = json::array();
            overJson.push_back("-");
            for (int i = 0; i < SIDE_LEN; ++i) {
                const auto& c    = overRow[i];
                const std::string name = idToName(c.symbolId);
                overJson.push_back(c.boostLevel == 2 ? (name + "**")
                                 : c.boostLevel == 1 ? (name + "*") : name);
            }
            overJson.push_back("-");
            screenJson.push_back(overJson);
        }

        for (int i = 0; i < maxHeight; ++i) {
            json rowJson = json::array();
            for (int j = 0; j < numReels; ++j) {
                if (i >= heights[j]) rowJson.push_back("-");
                else                 rowJson.push_back(idToName(grid[j][i]));
            }
            screenJson.push_back(rowJson);
        }

        if (includeUnder) {
            json underJson = json::array();
            underJson.push_back("-");
            for (int i = 0; i < SIDE_LEN; ++i) {
                const auto& c    = underRow[i];
                const std::string name = idToName(c.symbolId);
                underJson.push_back(c.boostLevel == 2 ? (name + "**")
                                 : c.boostLevel == 1 ? (name + "*") : name);
            }
            underJson.push_back("-");
            screenJson.push_back(underJson);
        }

        return screenJson;
    }

    void cascadeSideRow(bool over, ReelSet& rs, int boostProb, int superBoostProb = 0) {
        auto& row          = over ? overRow : underRow;
        const auto& strip  = rs.reels[0].symbolIds;
        const int N        = static_cast<int>(strip.size());
        if (N == 0) return;

        int left = rs.currentIndices[0];
        int next = (left + SIDE_LEN) % N;

        for (int pos = 0; pos < SIDE_LEN; ++pos) {
            while (row[pos].symbolId == EMPTY_SYM) {
                for (int p = pos; p < SIDE_LEN - 1; ++p)
                    row[p] = row[p + 1];

                int boostLevel = 0;
                int roll = getRand("TB", 100);
                if      (roll < superBoostProb) boostLevel = 2;
                else if (roll < boostProb)      boostLevel = 1;
                row[SIDE_LEN - 1] = SideCell{ strip[next], boostLevel };

                left = (left + 1) % N;
                next = (next + 1) % N;
            }
        }
        rs.currentIndices[0] = left;
    }

    void cascadeSymbols(ReelSet& reelSet, bool useDifferentReelSet, ReelSet& alternateReelSet) {
        ReelSet& activeReelSet = useDifferentReelSet ? alternateReelSet : reelSet;

        for (int reel = 0; reel < numReels; ++reel) {
            const auto& stripIds = activeReelSet.reels[reel].symbolIds;
            const int   stripLen = static_cast<int>(stripIds.size());
            for (int row = heights[reel] - 1; row >= 0; --row) {
                while (grid[reel][row] == EMPTY_SYM) {
                    for (int aboveRow = row; aboveRow > 0; aboveRow--)
                        grid[reel][aboveRow] = grid[reel][aboveRow - 1];
                    activeReelSet.currentIndices[reel]--;
                    if (activeReelSet.currentIndices[reel] < 0)
                        activeReelSet.currentIndices[reel] = stripLen - 1;
                    grid[reel][0] = stripIds[activeReelSet.currentIndices[reel]];
                }
            }
        }
    }

    // Add symbols to over/under rows from integrated ReelSet (uses precomputed symbolIds).
    void addSideSymbolsFromIntegratedReelSet(const ReelSet& rs,
        const std::vector<bool>& overBoostVec  = { 0,0,0,0 },
        const std::vector<bool>& underBoostVec = { 0,0,0,0 }) {
        if (rs.hasOverReel()) {
            const auto& overStrip = rs.getOverReel()->symbolIds;
            for (int i = 0; i < SIDE_LEN; ++i)
                setSideSymbol(true, i, overStrip[(rs.currentOverIndex + i) % overStrip.size()], overBoostVec[i]);
        }
        if (rs.hasUnderReel()) {
            const auto& underStrip = rs.getUnderReel()->symbolIds;
            for (int i = 0; i < SIDE_LEN; ++i)
                setSideSymbol(false, i, underStrip[(rs.currentUnderIndex + i) % underStrip.size()], underBoostVec[i]);
        }
    }

    // Cascade for integrated over/under reels
    void cascadeSideRowIntegrated(bool over, ReelSet& rs, const std::vector<int>& boostWeights) {
        if (over && !rs.hasOverReel())  return;
        if (!over && !rs.hasUnderReel()) return;

        auto& row         = over ? overRow : underRow;
        const auto& strip = over ? rs.getOverReel()->symbolIds : rs.getUnderReel()->symbolIds;
        const int N       = static_cast<int>(strip.size());
        if (N == 0) return;

        int& currentIndex = over ? rs.currentOverIndex : rs.currentUnderIndex;
        int left = currentIndex;
        int next = (left + SIDE_LEN) % N;

        for (int pos = 0; pos < SIDE_LEN; ++pos) {
            while (row[pos].symbolId == EMPTY_SYM) {
                for (int p = pos; p < SIDE_LEN - 1; ++p)
                    row[p] = row[p + 1];

                int boostLevel;
                if (boostWeights[2] == 100)
                    boostLevel = 2;
                else
                    boostLevel = getRandFromDist(std::string("BoostT_") + (over ? "O" : "U"), boostWeights);
                row[SIDE_LEN - 1] = SideCell{ strip[next], boostLevel };

                left = (left + 1) % N;
                next = (next + 1) % N;
            }
        }
        currentIndex = left;
    }

    // Add side symbols from a ReelSet (integrated or legacy single-reel).
    void addSideSymbols(bool over, const ReelSet& rs, const std::vector<int>& boostVec = { 0,0,0,0 }) {
        if (over && rs.hasOverReel()) {
            const auto& strip = rs.getOverReel()->symbolIds;
            for (int i = 0; i < SIDE_LEN; ++i)
                setSideSymbol(over, i, strip[(rs.currentOverIndex + i) % strip.size()], boostVec[i]);
        } else if (!over && rs.hasUnderReel()) {
            const auto& strip = rs.getUnderReel()->symbolIds;
            for (int i = 0; i < SIDE_LEN; ++i)
                setSideSymbol(over, i, strip[(rs.currentUnderIndex + i) % strip.size()], boostVec[i]);
        } else {
            // Fallback: legacy single-reel side strip in reels[0]
            const auto& strip = rs.reels[0].symbolIds;
            for (int i = 0; i < SIDE_LEN; ++i)
                setSideSymbol(over, i, strip[(rs.currentIndices[0] + i) % strip.size()], boostVec[i]);
        }
    }

    void markPosition(int reel, int row) {
        markedPositions.push_back(make_pair(reel, row));
    }

    void clearMarkedPositions() {
        markedPositions.clear();
    }

    const std::vector<std::pair<int, int>>& getMarkedPositions() const {
        return markedPositions;
    }

    // Mark all positions matching a symbol ID up to the given length of reels.
    void markSymbol(uint8_t id, int length, bool includeWild = true) {
        for (int i = 0; i < length; ++i) {
            for (int j = 0; j < heights[i]; ++j) {
                if (match(grid[i][j], id, includeWild))
                    markedPositions.push_back(make_pair(i, j));
            }
            if (middleReel(i)) {
                if (match(getSideSymbol(true,  i - 1), id, includeWild))
                    markedPositions.emplace_back(i, -1);   // -1 = overRow sentinel
                if (match(getSideSymbol(false, i - 1), id, includeWild))
                    markedPositions.emplace_back(i, -2);   // -2 = underRow sentinel
            }
        }
    }

    void removeMarkedPositions() {
        for (const auto& position : markedPositions) {
            int reel = position.first;
            int row  = position.second;
            if (row >= 0 && row < heights[reel]) {
                grid[reel][row] = EMPTY_SYM;
            } else if (middleReel(reel)) {
                if (row == -1) overRow [reel - 1] = SideCell{};
                if (row == -2) underRow[reel - 1] = SideCell{};
            }
        }
    }

    void fillMarkedSymbols(uint8_t id) {
        for (const auto& position : markedPositions) {
            int reel = position.first;
            int row  = position.second;
            grid[reel][row] = id;
        }
    }
};
