#pragma once
#include "GameConfig.h"
#include "Stats.h"
#include "Screen.h"
#include <atomic>

class GameInstance {

private:
	std::shared_ptr<GameConfig> config;  // Assuming you have a GameConfig class
	Stats& stats;                        // Assuming you have a Stats class
	SymbolStructure symbolStructure;
	std::string rtpKey;

	// Game parameters
	int numRows;
	int numReels;
	vector<PrizeDistribution<int>> reelHeightPD, reelHeightFreePD;
	int cost;
	std::vector<std::string> symbols;
	std::map<std::string, std::vector<int>> paytable;
	std::vector<std::string> payHeaders;

	std::vector<std::vector<int>> boostWeights;
	std::vector<PrizeDistribution<int>> boostOverPDVec, boostUnderPDVec;
	std::vector<PrizeDistribution<int>> boostOverPDVecFree, boostUnderPDVecFree;
	std::vector<int> boostVecOver, boostVecUnder, cascadeWeights, cascadeWeightsFree;
	enum class GameMode {
		REGULAR,
		ANTE1,
		ANTE2,
		BUY0,
		BUY1,
		BOOST
	};
	GameMode gameMode = GameMode::REGULAR;
	PrizeDistribution<int> forceScattersPD;
	ReelSet boostReelSet;
	// ReelSets
	ReelSet baseReelSet, tumbleReelSet, noWinReelSet, overReelSet, underReelSet;
	std::unordered_map<std::string, ReelSet> allReelSets;
	// Cached copies indexed by reelID so the spin loop can use a pointer
	// instead of doing a full deep-copy of the ReelSet on every spin.
	std::vector<ReelSet> cachedBaseReels;  // [0]=baseLow … [5]=freeTrigger
	std::vector<ReelSet> cachedFreeReels;  // [0]=freeLow  … [4]=noWinX
	std::vector<int> reelWeights, reelWeightsFree;
	PrizeDistribution<int> ReelsPD, ReelsFreePD, superBoostPD;
	vector<PrizeDistribution<double>> moneyPrizes;
	// Game variables
	Screen screen;
	int tumbleCount;
	int spinCount;
	int baseTumbleCount;
	int lastReelSetID = -1;
	uint8_t scatterSymId = EMPTY_SYM;  // precomputed ID of the scatter/F1 symbol




	enum PayIdx {
		INITIAL = 0,
		TUMBLE,
		BASE,
		FREE_TOTAL,
		TOTAL
	};

	static GameMode parseGameMode(const std::string& mode) {
		if (mode == "regular") return GameMode::REGULAR;
		if (mode == "ante1") return GameMode::ANTE1;
		if (mode == "ante2") return GameMode::ANTE2;
		if (mode == "buy0") return GameMode::BUY0;
		if (mode == "buy1") return GameMode::BUY1;
		if (mode == "boost") return GameMode::BOOST;
		return GameMode::REGULAR;
	}

	bool isBoostMode() const { return gameMode == GameMode::BOOST; }
	bool isBuyMode() const { return gameMode == GameMode::BUY0 || gameMode == GameMode::BUY1; }

	void initializeGame() {
		{
			rtpKey = config->parseVar<std::string>("RTP");
			//numRows = config->parseVar<int>("rows");
			numReels = config->parseVar<int>("reels");
			reelHeightPD = config->parsePDVec<int>("reelHeights");
			reelHeightFreePD = config->parsePDVec<int>("reelHeightsFree");
			boostOverPDVec = config->parsePDVec<int>("boostWeightsOver");
			boostUnderPDVec = config->parsePDVec<int>("boostWeightsUnder");
			boostOverPDVecFree = config->parsePDVec<int>("boostWeightsOverFree");
			boostUnderPDVecFree = config->parsePDVec<int>("boostWeightsUnderFree");
			superBoostPD = config->parsePrizeDistribution<int>("superBoost");
			gameMode = parseGameMode(config->parseVar<std::string>("gameMode"));
			
			cascadeWeights = config->parseVec<int>("cascadeWeights");
			cascadeWeightsFree = config->parseVec<int>("cascadeWeightsFree");
			//boostWeights = config->parseArray<int>("boostWeights");
			payHeaders = config->getRTPHeaders();
			symbolStructure = config->parseSymbolStructure();
			allReelSets = config->parseAllReelSets();
			if (isBoostMode()) {
				forceScattersPD = config->parsePrizeDistribution<int>("forceScatters");
				boostReelSet = allReelSets["bonusBoost"];
			}
			/* baseReelSet = config->parseReelSet("baseLow");
			 tumbleReelSet = config->parseReelSet("tumbleHigh");*/
			if (!isBoostMode() && !isBuyMode()) {
				reelWeights = config->parseVec<int32_t>("reelWeights", rtpKey);
			}
			reelWeightsFree = config->parseVec<int32_t>("reelWeightsFree", rtpKey);
			ReelsPD = PrizeDistribution<int>("R-WTS", std::vector<int>{0, 1, 2, 3, 4, 5}, reelWeights);
			ReelsFreePD = PrizeDistribution<int>("FR-WTS", std::vector<int>{0, 1, 2, 3, 4}, reelWeightsFree);
			cost = config->parseVar<int>("cost");
			symbols = symbolStructure.getSymbols();
			paytable = symbolStructure.getPaytable();
			//screen.resize(reelHeights);

			// Build the reel-set caches (one-time copies, same order as the
			// switch statements in playBaseGame / playFreeGames).
			if (isBuyMode()) {
				cachedBaseReels = {
					allReelSets["buyBase"]
				};
			}
			else {
				cachedBaseReels = {
					allReelSets["baseLow"],
					allReelSets["baseHigh"],
					allReelSets["tumbleLow"],
					allReelSets["tumbleHigh"],
					allReelSets["noWinX"],
					allReelSets["freeTrigger"]
				};
			}
			cachedFreeReels = {
				allReelSets["freeLow"],
				allReelSets["freeHigh"],
				allReelSets["tumbleLow"],
				allReelSets["tumbleHigh"],
				allReelSets["noWinX"]
			};

			// Build uint8_t symbolId arrays on every reel strip (symbol interning).
			// Done once here so the hot-path spin/cascade code never touches std::string.
			const auto& lookup = symbolStructure.getLookup();
			for (auto& rs : cachedBaseReels) rs.buildSymbolIds(lookup);
			for (auto& rs : cachedFreeReels) rs.buildSymbolIds(lookup);
			for (auto& kv : allReelSets)     kv.second.buildSymbolIds(lookup);
			if (isBoostMode()) boostReelSet.buildSymbolIds(lookup);

			// Register the wild symbol and initialise the screen name table.
			symbolStructure.setWild("WL");
			screen.init(symbols, symbolStructure.getWildId());

			// Precompute scatter (F1) symbol ID for playBaseGame hot path.
			scatterSymId = static_cast<uint8_t>(symbolStructure.findSymbolIndex("F1"));
		}

	};

public:

	explicit GameInstance(std::shared_ptr<GameConfig> config, SymbolStructure& symbolStructure, Stats& stats)
		: config(config), symbolStructure(symbolStructure), stats(stats),
		spinCount(0) {
		initializeGame();
	}


	// Method to simulate a single spin and return the result
	double simulateSingleSpin() {
		std::atomic<long long> dummy{0};
		playBaseGame(1, dummy);  // Simulate one spin
		double lastSpinPayout = stats.getLastSpinPayout();  // Retrieve payout from the last spin
		return lastSpinPayout - cost;  // Return net gain/loss (payout minus cost of one spin)
	}


	void playBaseGame(long long numSpins, std::atomic<long long>& spinsDone) {
		vector<double> baseVector, freeVector;
		double basePay, tempPay;
		ReelSet* activeReelsPtr = nullptr;  // points into cachedBaseReels — no copy per spin
		int globalMult;

		RandomLogGenerator::setMaxRoundWin(200000);

		/*	boostPDVec.resize(boostWeights.size());
			for (int i = 0; i < boostWeights.size(); ++i) {
				boostPDVec[i] = PrizeDistribution<int>("BS_" + std::to_string(i + 1),
					std::vector<int>{0, 1}, boostWeights[i]);
			}*/

		for (long long i = 0; i < numSpins; ++i) {
			basePay = 0;
			globalMult = 1;
			baseTumbleCount = 0;

			RandomLogGenerator::startRound();
			std::vector<double> pays(payHeaders.size(), 0);
			std::vector<int> reelHeights(numReels);
			if (isBuyMode()) {
				reelHeights = {4, 4, 4, 2, 2, 2};
			}
			else {
				for (int r = 0; r < numReels; ++r) {
					reelHeights[r] = reelHeightPD[r].getRandomPrize();
				}
			}
			screen.resize(reelHeights);


			if (isBoostMode()) {
				activeReelsPtr = &boostReelSet;
				lastReelSetID = -1;
			}
			else if (isBuyMode()) {
				lastReelSetID = 0;
				activeReelsPtr = &cachedBaseReels[0];
			}
			else {
				int reelID = ReelsPD.getRandomPrize();
				lastReelSetID = reelID;
				activeReelsPtr = &cachedBaseReels[reelID];
			}

			// This now spins main reels AND over/under reels if they exist
			activeReelsPtr->spinReels();

			// Determine boost for over/under reels (prizes: 0=none, 1=regular, 2=superboost)
			boostVecOver.clear();
			boostVecUnder.clear();
			for (int b = 0; b < boostOverPDVec.size(); ++b) {
				boostVecOver.push_back(boostOverPDVec[b].getRandomPrize());				
			}
			for (int b = 0; b < boostUnderPDVec.size(); ++b) {
				boostVecUnder.push_back(boostUnderPDVec[b].getRandomPrize());
			}

			// Generate main screen
			screen.generateScreen(*activeReelsPtr);

			// Add side symbols from the integrated reelset
			if (activeReelsPtr->hasOverReel()) {
				screen.addSideSymbols(true, *activeReelsPtr, boostVecOver);
			}
			if (activeReelsPtr->hasUnderReel()) {
				screen.addSideSymbols(false, *activeReelsPtr, boostVecUnder);
			}

			auto rollSuperboost = [this]() { return superBoostPD.getRandomPrize(); };
			screen.assignSuperboostMultipliers(rollSuperboost);

			if (isBoostMode()) {
				int numScatters = forceScattersPD.getRandomPrize();
				vector<int> scatterReels = getRandomPositions("SR", numReels, numScatters);
				for (int s = 0; s < numScatters && s < numReels; ++s) {
					int reel = scatterReels[s];
					int row = getRand("SC_" + std::to_string(reel), screen.getReelHeight(reel));
					screen.updateCell(reel, row, scatterSymId);
				}
			}

			baseVector = handleCascades(screen, *activeReelsPtr, *activeReelsPtr, false, true, globalMult);
			basePay = baseVector[0] + baseVector[1];

			if (basePay)
				stats.trackFeatureActivation("Base Win");

			pays[INITIAL] += baseVector[0];
			pays[TUMBLE] += baseVector[1];
			pays[BASE] += basePay;

			
			//RandomLogGenerator::addScreen(screen.toJson(true, true)); // just to show scatters in the log when they are forced by the boost mode
			int fgCount = screen.countSymbolOnScreen(scatterSymId, false);
			if (fgCount >= 3) {
				freeVector = playFreeGames(5 * (fgCount - 3) + 10, 1);
				stats.trackFeatureActivation("FS Trigger " + to_string(fgCount));
				stats.trackFeatureActivation("Free Spins");
				pays[FREE_TOTAL] += freeVector[0];
			}
			else if (fgCount == 2) {
				stats.trackFeatureActivation("FS Tease");
			}

			RandomLogGenerator::endRound();
			//pays[TOTAL] = std::accumulate(pays.begin(), pays.end() - 2, 0.0);
			pays[TOTAL] = pays[INITIAL] + pays[TUMBLE] + pays[FREE_TOTAL];

			// Cap TOTAL before sending to Stats (works in LOGGING and NO_LOGGING)
			const double cap = RandomLogGenerator::maxRoundWin;            // public static
			if (pays[TOTAL] > cap)
				pays[TOTAL] = cap;

			if (pays[TOTAL])
				stats.trackFeatureActivation("Base");

			stats.completeWager(pays);
			spinsDone.fetch_add(1, std::memory_order_relaxed);
		}
	}

	vector<double> playFreeGames(int numFreeGames, int initMult) {
		vector<double> pays(2, 0);
		vector<double> tempPays;
		int multiplier = initMult;
		int freeSpinsRemaining = numFreeGames;
		int reelID;



		Screen screen(numReels, numRows);
		screen.init(symbols, symbolStructure.getWildId());
		screen.clearScreen();


		while (freeSpinsRemaining > 0) {
			tumbleCount = 0;
			RandomLogGenerator::newSpin();

			std::vector<int> reelHeights(numReels);
			for (int r = 0; r < numReels; ++r) {
				reelHeights[r] = reelHeightFreePD[r].getRandomPrize();
				//reelHeights[r] = reelHeightPD[r].getRandomPrize();
			}
			screen.resize(reelHeights);

			reelID = ReelsFreePD.getRandomPrize();

			// Point at the cached copy — no deep-copy of reel strips every free spin.
			ReelSet* freeReelSetPtr = &cachedFreeReels[reelID];

			freeReelSetPtr->spinReels();

			// All over/under symbols are boosted - use free game boost distributions
			boostVecOver.clear();
			boostVecUnder.clear();
			for (int b = 0; b < boostOverPDVecFree.size(); ++b) {
				boostVecOver.push_back(boostOverPDVecFree[b].getRandomPrize());
			}
			for (int b = 0; b < boostUnderPDVecFree.size(); ++b) {
				boostVecUnder.push_back(boostUnderPDVecFree[b].getRandomPrize());
			}

			screen.generateScreen(*freeReelSetPtr);
			screen.addSideSymbols(true, *freeReelSetPtr, boostVecOver);
			screen.addSideSymbols(false, *freeReelSetPtr, boostVecUnder);

			// NEW:
			auto rollSuperboost = [this]() { return superBoostPD.getRandomPrize(); };
			screen.assignSuperboostMultipliers(rollSuperboost);

			// Handle cascades for free spins
			tempPays = handleCascades(screen, *freeReelSetPtr, *freeReelSetPtr, false, false, multiplier);
			pays[0] += tempPays[0];
			pays[0] += tempPays[1];

			// Individual free spin hit rate tracking
			bool spinHit = (tempPays[0] + tempPays[1] > 0);
			stats.recordFreeSpin(spinHit);

			freeSpinsRemaining--;

		}

		//stats.recordTumbleFrequency(tumbleCount);
		stats.recordFreeSpins(numFreeGames);
		stats.recordFinalMultFree(multiplier);
		stats.recordFinalMultFreeByInit(initMult, multiplier);

		return pays;
	}

	std::pair<int, bool> boostsInWin(const Screen& screen, bool baseGame) {
		int multIncrease = 0;
		bool hasSuperboost = false;
		const auto& marked = screen.getMarkedPositions();
		for (const auto& pos : marked) {
			int reel = pos.first;
			int row = pos.second;
			// over side hit
			if (row == -1) {
				int boostLevel = screen.getSideBoostLevel(true, reel - 1);
				if (boostLevel == 1) multIncrease += 1;       // Regular boost: +1
				else if (boostLevel == 2) {
					multIncrease += screen.getSideMultiplier(true, reel - 1);  // use pre-assigned value
					hasSuperboost = true;
				}

				// Track boost activations in free games
				if (!baseGame && boostLevel > 0) {
					stats.recordBoostActivationFree(boostLevel);
				}
			}
			// under side hit
			if (row == -2) {
				int boostLevel = screen.getSideBoostLevel(false, reel - 1);
				if (boostLevel == 1) multIncrease += 1;       // Regular boost: +1
				else if (boostLevel == 2) {
					multIncrease += screen.getSideMultiplier(false, reel - 1);  // use pre-assigned value

					hasSuperboost = true;
				}

				// Track boost activations in free games
				if (!baseGame && boostLevel > 0) {
					stats.recordBoostActivationFree(boostLevel);
				}
			}
		}
		return { multIncrease, hasSuperboost };
	}

	vector<double> handleCascades(Screen& screen, ReelSet& reelSet, ReelSet& offScreenReelSet,
		bool useDifferentReelSet, bool baseGame, int& globalMult) {
		bool hasNewWins;
		double initialWin = 0, tumbleWin = 0, tempWin;
		int tumbleCount = 0;
		bool hasSuperboostInWin = false;

		if (useDifferentReelSet) {
			offScreenReelSet.spinReels();
		}

		do {
			hasNewWins = false;
			screen.clearMarkedPositions();
			tempWin = 0;

			if (tumbleCount == 0) {
				initialWin = calculateWaysWins(screen, baseGame);
				std::pair<int, bool> boostInfo = boostsInWin(screen, baseGame);
				int multIncrease = boostInfo.first;
				bool hasSuperboost = boostInfo.second;
				globalMult += multIncrease;
				if (hasSuperboost) hasSuperboostInWin = true;
				initialWin *= globalMult;
				RandomLogGenerator::addWinAmount(initialWin);
			}
			else {
				tempWin = calculateWaysWins(screen, baseGame);
				std::pair<int, bool> boostInfo = boostsInWin(screen, baseGame);
				int multIncrease = boostInfo.first;
				bool hasSuperboost = boostInfo.second;
				globalMult += multIncrease;
				if (hasSuperboost) hasSuperboostInWin = true;
				tempWin *= globalMult;
				tumbleWin += tempWin;
				RandomLogGenerator::addWinAmount(tempWin);
			}

			if (!screen.getMarkedPositions().empty()) {
				hasNewWins = true;
				tumbleCount++;

				screen.removeMarkedPositions();
				screen.cascadeSymbols(reelSet, useDifferentReelSet, offScreenReelSet);

				// Updated cascade calls - use the integrated over/under reels
				if (reelSet.hasOverReel()) {
					//screen.cascadeSideRowIntegrated(true, reelSet, 50);
					screen.cascadeSideRowIntegrated(true, reelSet, baseGame ? cascadeWeights : cascadeWeightsFree);
				}
				if (reelSet.hasUnderReel()) {
					screen.cascadeSideRowIntegrated(false, reelSet, baseGame ? cascadeWeights : cascadeWeightsFree);
				}
				// NEW: assign multipliers to any newly cascaded superboost symbols
				auto rollSuperboost = [this]() { return superBoostPD.getRandomPrize(); };
				screen.assignSuperboostMultipliers(rollSuperboost);
			}
		} while (hasNewWins);


		if (initialWin) {
			stats.recordTumbleFrequency(tumbleCount, baseGame);
			// Record if this win had a superboost
			stats.recordWinWithSuperboost(baseGame, hasSuperboostInWin);
		}
		if (baseGame) {
			stats.recordFinalMult(globalMult);
			baseTumbleCount = tumbleCount;
		}



		return { initialWin, tumbleWin };
	}

	double calculateWaysWins(Screen& screen, bool baseGame, int currentMult = 1) {
		double totalPay = 0;

		if ((simulationMode == LOG_MODE || simulationMode == REPLAY_MODE) && RandomLogGenerator::logGameDetails) {
			RandomLogGenerator::addScreen(screen.toJson(true, true));
		}
		// Clear previous marked positions
		screen.clearMarkedPositions();

		// Hot path: iterate by integer ID — no string comparisons inside the loop.
		const auto& paytableVec = symbolStructure.getPaytableVec();
		const int numSymbols = static_cast<int>(symbols.size());
		for (int id = 0; id < numSymbols; ++id) {
			const uint8_t uid = static_cast<uint8_t>(id);
			auto waysInfo = screen.getWaysForSymbol(uid);
			int length = waysInfo.first;
			int ways   = waysInfo.second;
			int payout = 0;

			if (length > 0) {
				payout = currentMult * ways * paytableVec[id][length - 1];
				if (payout > 0) {
					stats.trackResult(symbols[id], length, ways, payout, baseGame);
					screen.markSymbol(uid, length);
				}
			}
			totalPay += payout;
		}

		//RandomLogGenerator::addWinAmount(totalPay);
		return totalPay;
	}

	//double calculateLineWins(Screen& screen, bool baseGame) {
	//	double totalPay = 0;
	//	RandomLogGenerator::addScreen(screen.toJson());
	//	// Clear previous marked positions
	//	screen.clearMarkedPositions();
	//	// Evaluate each payline
	//	for (int lineIndex = 0; lineIndex < screen.getNumPaylines(); ++lineIndex) {
	//		std::string sym;
	//		int         len = 0;
	//		double pay = screen.evaluatePaylinePay(lineIndex, paytable, sym, len);
	//		if (pay > 0)
	//		{
	//			stats.trackResult(sym, len, 1, pay, baseGame);
	//			screen.markPayline(lineIndex, len);
	//			totalPay += pay;
	//		}
	//		//std::tuple<std::string, int, bool> result = screen.evaluatePayline(lineIndex);
	//		//std::string symbol = std::get<0>(result);
	//		//int length = std::get<1>(result);
	//		//bool isWinning = std::get<2>(result);
	//		//if (isWinning && length > 0 && !symbol.empty()) {
	//		//	// Look up the pay for this symbol and length
	//		//	if (paytable.find(symbol) != paytable.end() && length <= paytable[symbol].size()) {
	//		//		int payout = paytable[symbol][length - 1];
	//		//		if (payout > 0) {
	//		//			stats.trackResult(symbol, length, 1, payout, baseGame);
	//		//			screen.markPayline(lineIndex, length);
	//		//			totalPay += payout;
	//		//		}
	//		//	}
	//		//}
	//	}
	//	return totalPay;
	//}

	int getLastReelSetID() const {
		return lastReelSetID;
	}
	int getBaseTumbleCount() const {
		return baseTumbleCount;
	}
};

class PlayerSimulation {
public:
	PlayerSimulation(int startCredits, int targetCredits, std::shared_ptr<GameConfig> config, SymbolStructure& symbolStructure, Stats& stats)
		: startCredits(startCredits), targetCredits(targetCredits), gameConfig(config), symbolStructure(symbolStructure), stats(stats) {
	}

	bool simulate() {
		int currentCredits = startCredits;
		GameInstance gameInstance(gameConfig, symbolStructure, stats);  // Updated GameInstance initialization

		while (currentCredits > 0 && currentCredits < targetCredits) {
			double result = gameInstance.simulateSingleSpin();  // Simulate spin and get net result
			currentCredits += result;  // Update current credits

			if (result > 0) {
				stats.recordWin(result);  // Track wins in Stats if result is positive
			}
		}
		return currentCredits >= targetCredits;
	}

private:
	int startCredits;
	int targetCredits;
	std::shared_ptr<GameConfig> gameConfig;
	SymbolStructure& symbolStructure;
	Stats& stats;  // Reference to the shared Stats object
};
