#pragma once
#include "GameConfig.h"
#include "Stats.h"
#include "Screen.h"

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
	std::vector<int> boostVecOver, boostVecUnder;
	// ReelSets
	ReelSet baseReelSet, tumbleReelSet, noWinReelSet, overReelSet, underReelSet;
	std::unordered_map<std::string, ReelSet> allReelSets;
	std::vector<int> reelWeights, reelWeightsFree;
	PrizeDistribution<int> ReelsPD, ReelsFreePD;
	vector<PrizeDistribution<double>> moneyPrizes;
	// Game variables
	Screen screen;
	int tumbleCount;
	int spinCount;
	int baseTumbleCount;
	int lastReelSetID = -1;




	enum PayIdx {
		INITIAL = 0,
		TUMBLE,
		BASE,
		FREE_TOTAL,
		TOTAL
	};

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
			//boostWeights = config->parseArray<int>("boostWeights");
			payHeaders = config->getRTPHeaders();
			symbolStructure = config->parseSymbolStructure();
			allReelSets = config->parseAllReelSets();
			/* baseReelSet = config->parseReelSet("baseLow");
			 tumbleReelSet = config->parseReelSet("tumbleHigh");*/
			reelWeights = config->parseVec<int32_t>("reelWeights", rtpKey);
			reelWeightsFree = config->parseVec<int32_t>("reelWeightsFree", rtpKey);
			ReelsPD = PrizeDistribution<int>("R-WTS", std::vector<int>{0, 1, 2, 3, 4}, reelWeights);
			ReelsFreePD = PrizeDistribution<int>("FR-WTS", std::vector<int>{0, 1, 2, 3}, reelWeightsFree);
			cost = config->parseVar<int>("cost");
			symbols = symbolStructure.getSymbols();
			paytable = symbolStructure.getPaytable();
			//screen.resize(reelHeights);

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
		playBaseGame(1);  // Simulate one spin
		double lastSpinPayout = stats.getLastSpinPayout();  // Retrieve payout from the last spin
		return lastSpinPayout - cost;  // Return net gain/loss (payout minus cost of one spin)
	}


	void playBaseGame(long long numSpins) {
		vector<double> baseVector, freeVector;
		double basePay, tempPay;
		ReelSet activeReels;
		int globalMult;

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
			for (int r = 0; r < numReels; ++r) {
				reelHeights[r] = reelHeightPD[r].getRandomPrize();
			}
			screen.resize(reelHeights);


			int reelID = ReelsPD.getRandomPrize();
			//reelID = 0;
			lastReelSetID = reelID;
			switch (reelID) {
			case 0:
				//activeReels = allReelSets["baseLow"]; 
				activeReels = allReelSets["baseLow"];
				break;
			case 1:
				activeReels = allReelSets["baseHigh"];
				break;
			case 2:
				activeReels = allReelSets["freeTumbleLow"];
				break;
			case 3:
				activeReels = allReelSets["freeTumbleHigh"];
				break;
			case 4:
				activeReels = allReelSets["noWin1"];
				break;
			}

			// This now spins main reels AND over/under reels if they exist
			activeReels.spinReels();

			// Determine boost for over/under reels (prizes: 0=none, 1=regular, 2=superboost)
			boostVecOver.clear();
			boostVecUnder.clear();
			for (int b = 0; b < boostOverPDVec.size(); ++b) {
				boostVecOver.push_back(boostOverPDVec[b].getRandomPrize());
				boostVecUnder.push_back(boostUnderPDVec[b].getRandomPrize());
			}

			// Generate main screen
			screen.generateScreen(activeReels);

			// Add side symbols from the integrated reelset
			if (activeReels.hasOverReel()) {
				screen.addSideSymbols(true, activeReels, boostVecOver);
			}
			if (activeReels.hasUnderReel()) {
				screen.addSideSymbols(false, activeReels, boostVecUnder);
			}

			baseVector = handleCascades(screen, activeReels, activeReels, false, true, globalMult);
			basePay = baseVector[0] + baseVector[1];

			if (basePay)
				stats.trackFeatureActivation("Base Win");

			pays[INITIAL] += baseVector[0];
			pays[TUMBLE] += baseVector[1];
			pays[BASE] += basePay;

			int fgCount = screen.countSymbolOnScreen("F1", false);
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

			if (pays[TOTAL])
				stats.trackFeatureActivation("Base");

			stats.completeWager(pays);
		}
	}

	vector<double> playFreeGames(int numFreeGames, int initMult) {
		vector<double> pays(2, 0);
		vector<double> tempPays;
		int multiplier = initMult;
		int freeSpinsRemaining = numFreeGames;

		ReelSet freeReelSet;

		// All over/under symbols are boosted - use free game boost distributions
		boostVecOver.clear();
		boostVecUnder.clear();
		for (int b = 0; b < boostOverPDVecFree.size(); ++b) {
			boostVecOver.push_back(boostOverPDVecFree[b].getRandomPrize());
			boostVecUnder.push_back(boostUnderPDVecFree[b].getRandomPrize());
		}

		Screen screen(numReels, numRows);
		screen.clearScreen();


		while (freeSpinsRemaining > 0) {
			tumbleCount = 0;
			RandomLogGenerator::newSpin();

			std::vector<int> reelHeights(numReels);
			for (int r = 0; r < numReels; ++r) {
				//reelHeights[r] = reelHeightFreePD[r].getRandomPrize();
				reelHeights[r] = reelHeightPD[r].getRandomPrize();
			}
			screen.resize(reelHeights);

			int reelID = ReelsFreePD.getRandomPrize();

			switch (reelID) {
			case 0:
				//activeReels = allReelSets["baseLow"]; 
				freeReelSet = allReelSets["freeLow"];
				break;
			case 1:
				freeReelSet = allReelSets["freeHigh"];
				break;
			case 2:
				//freeReelSet = allReelSets["baseTumble"];
				freeReelSet = allReelSets["freeTumbleLow"];
				break;
			case 3:
				freeReelSet = allReelSets["freeTumbleHigh"];
				break;
			}


			freeReelSet.spinReels();

			screen.generateScreen(freeReelSet);
			screen.addSideSymbols(true, freeReelSet, boostVecOver);
			screen.addSideSymbols(false, freeReelSet, boostVecUnder);

			// Handle cascades for free spins
			tempPays = handleCascades(screen, freeReelSet, freeReelSet, false, false, multiplier);
			pays[0] += tempPays[0];
			pays[0] += tempPays[1];

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
					multIncrease += 10; // Superboost: +10
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
					multIncrease += 10; // Superboost: +10
					hasSuperboost = true;
				}

				// Track boost activations in free games
				if (!baseGame && boostLevel > 0) {
					stats.recordBoostActivationFree(boostLevel);
				}
			}
		}
		return {multIncrease, hasSuperboost};
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
				auto [multIncrease, hasSuperboost] = boostsInWin(screen, baseGame);
				globalMult += multIncrease;
				if (hasSuperboost) hasSuperboostInWin = true;
				initialWin *= globalMult;
				RandomLogGenerator::addWinAmount(initialWin);
			}
			else {
				tempWin = calculateWaysWins(screen, baseGame);
				auto [multIncrease, hasSuperboost] = boostsInWin(screen, baseGame);
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
					screen.cascadeSideRowIntegrated(true, reelSet, baseGame ? 50 : 100);
				}
				if (reelSet.hasUnderReel()) {
					screen.cascadeSideRowIntegrated(false, reelSet, baseGame ? 50 : 100);
				}
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

		if (logMode != NO_LOGGING) {
			RandomLogGenerator::addScreen(screen.toJson(true, true));
		}
		// Clear previous marked positions
		screen.clearMarkedPositions();

		for (const auto& symbol : symbols) {
			auto waysInfo = screen.getWaysForSymbol(symbol);
			int length = waysInfo.first;
			int ways = waysInfo.second;
			int payout = 0;

			if (length > 0) {
				payout = currentMult * ways * paytable[symbol][length - 1];
				if (payout > 0) {
					stats.trackResult(symbol, length, ways, payout, baseGame);
					screen.markSymbol(symbol, length);
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