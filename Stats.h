#pragma once
#include "GameConfig.h" // Include GameConfig to access configuration data
#include <vector>
#include <algorithm>
#include <fstream>
#include <cmath> // Include for std::sqrt
#include <unordered_map>
#include <mutex>

#include <utility> // For std::pair
#include <functional> // For hash specialization

namespace std {
	template <>
	struct hash<std::pair<int, int>> {
		size_t operator()(const std::pair<int, int>& p) const {
			return std::hash<int>()(p.first) ^ (std::hash<int>()(p.second) << 1);
		}
	};
}

class Stats {
private:
	std::mutex statsMutex;

	long long numIterations;
	long long baseGameHits = 0;
	long long breakEvenOrBetterSpins = 0; // Track spins that pay >= cost
	double costPerSpin;
	double totalWin;
	std::vector<std::string> rtpHeaders;
	std::vector<double> payVector, lastPay;
	std::vector<std::unordered_map<double, long long>> payFrequencies;
	std::unordered_map<std::string, long long> featureHits;
	std::vector<std::vector<long long>> baseSymHits, freeSymHits;
	std::vector<std::vector<double>> baseSymPays, freeSymPays;
	std::unordered_map<int, long long> scatterHits, freeSpinsFreq, tumbleFreq, tumbleFreqFree, multFreq, multFreqFree;
	std::unordered_map<int, std::unordered_map<int, long long>> multFreqFreeByInit;

	// Boost tracking for free games
	std::unordered_map<int, long long> boostFreqFree; // Boost level (0, 1, 2) -> frequency
	long long standardBoostsFree = 0; // Count of standard boosts (+1) in free games
	long long superboostsFree = 0; // Count of superboosts (+10) in free games
	long long totalMultFromStandardBoostsFree = 0; // Total multiplier contribution from standard boosts
	long long totalMultFromSuperboostsFree = 0; // Total multiplier contribution from superboosts

	// Win tracking with superboosts
	long long winsWithSuperboostBase = 0; // Number of base game wins with at least one superboost
	long long winsWithSuperboostFree = 0; // Number of free game wins with at least one superboost
	long long totalWinsBase = 0; // Total base game wins (for percentage calculation)
	long long totalWinsFree = 0; // Total free game wins (for percentage calculation)

	SymbolStructure& symbolStructure;
	std::vector<double> standardDeviations;
	int totalWins = 0;
	double totalWinnings = 0.0;
	std::pair<int, double> moneyEntry; // <count, amount>

	// Individual free-spin hit tracking
	long long totalFreeSpinsPlayed = 0;
	long long freeSpinHits = 0;

	// Add inside class Stats (private section)
	template<typename Map>
	static void writeSortedFrequency(std::ofstream& file, const std::string& keyHeader, const std::string& valueHeader, const Map& freq) {
		using Key = typename Map::key_type;
		using Val = typename Map::mapped_type;
		std::vector<std::pair<Key, Val>> items;
		items.reserve(freq.size());
		for (const auto& kv : freq) items.emplace_back(kv.first, kv.second);
		std::sort(items.begin(), items.end(), [](const std::pair<Key, Val>& a, const std::pair<Key, Val>& b) {
			return a.first < b.first;
			});
		file << keyHeader << '\t' << valueHeader << '\n';
		for (const auto& p : items) {
			file << p.first << '\t' << p.second << '\n';
		}
	}

public:
	explicit Stats(SymbolStructure& symbolStructure, const std::vector<std::string>& rtpHeaders, double costPerSpin)
		: symbolStructure(symbolStructure), rtpHeaders(rtpHeaders), costPerSpin(costPerSpin) {
		numIterations = 0;
		totalWin = 0.0;

		size_t numRTPs = rtpHeaders.size();
		payVector.resize(numRTPs, 0.0);
		payFrequencies.resize(numRTPs);

		// featureHits.resize(featureNames.size(), 0);

		int numSymbols = symbolStructure.getNumSymbols();
		int maxLength = symbolStructure.getWinLength();
		baseSymHits.resize(numSymbols, std::vector<long long>(maxLength, 0));
		freeSymHits.resize(numSymbols, std::vector<long long>(maxLength, 0));
		baseSymPays.resize(numSymbols, std::vector<double>(maxLength, 0.0));
		freeSymPays.resize(numSymbols, std::vector<double>(maxLength, 0.0));
	}
	void setNumIterations(long long iterations) {
		std::lock_guard<std::mutex> lock(statsMutex);
		numIterations = iterations;
	}

	void trackResult(const std::string& symbol, int length, int ways, double pay, bool base) {
		std::lock_guard<std::mutex> lock(statsMutex);
		int symbolIndex = symbolStructure.findSymbolIndex(symbol);
		int lengthIndex = length - 1;
		if (base) {
			baseSymHits[symbolIndex][lengthIndex] += ways;
			baseSymPays[symbolIndex][lengthIndex] += pay;
		} else {
			freeSymHits[symbolIndex][lengthIndex] += ways;
			freeSymPays[symbolIndex][lengthIndex] += pay;
		}
	}

	void recordScatterHit(int prize) {
		std::lock_guard<std::mutex> lock(statsMutex);
		scatterHits[prize]++;
	}

	void recordTumbleFrequency(int tumbles, bool base) {
		std::lock_guard<std::mutex> lock(statsMutex);
		if (base) {
			tumbleFreq[tumbles]++;
		} else {
			tumbleFreqFree[tumbles]++;
		}
	}

	void recordFinalMult(int mult) {
		std::lock_guard<std::mutex> lock(statsMutex);
		multFreq[mult]++;
	}

	void recordFinalMultFree(int mult) {
		std::lock_guard<std::mutex> lock(statsMutex);
		multFreqFree[mult]++;
	}

	void recordFinalMultFreeByInit(int initMult, int finalMult) {
		std::lock_guard<std::mutex> lock(statsMutex);
		multFreqFreeByInit[initMult][finalMult]++;
	}

	// Record boost activation in free games
	void recordBoostActivationFree(int boostLevel) {
		std::lock_guard<std::mutex> lock(statsMutex);
		boostFreqFree[boostLevel]++;

		if (boostLevel == 1) {
			standardBoostsFree++;
			totalMultFromStandardBoostsFree += 1;
		} else if (boostLevel == 2) {
			superboostsFree++;
			totalMultFromSuperboostsFree += 10;
		}
	}

	// Record when a win has a superboost applied
	void recordWinWithSuperboost(bool baseGame, bool hasSuperboost) {
		std::lock_guard<std::mutex> lock(statsMutex);
		if (baseGame) {
			totalWinsBase++;
			if (hasSuperboost) {
				winsWithSuperboostBase++;
			}
		} else {
			totalWinsFree++;
			if (hasSuperboost) {
				winsWithSuperboostFree++;
			}
		}
	}

	// calculate multiplier hit rate (when mult > 1)
	double calculateMultiplierHitRate() const {
		long long multHits = 0;
		long long totalOccurrences = 0;
		for (const auto& pair : multFreq) {
			if (pair.first > 1) {
				multHits += pair.second;
			}
			totalOccurrences += pair.second;
		}
		if (totalOccurrences == 0) {
			return 0.0;
		}
		return static_cast<double>(totalOccurrences) / multHits;
	}



	//record number of free spins
	void recordFreeSpins(int freeSpins) {
		std::lock_guard<std::mutex> lock(statsMutex);
		freeSpinsFreq[freeSpins]++;
	}

	// Record a single free spin result (used to compute individual free-spin hit rate)
	void recordFreeSpin(bool hit) {
		std::lock_guard<std::mutex> lock(statsMutex);
		totalFreeSpinsPlayed++;
		if (hit) freeSpinHits++;
	}

	// Return ratio of free spins that had any win (0..1)
	double getFreeSpinHitRate() const {
		//std::lock_guard<std::mutex> lock(statsMutex);
		if (totalFreeSpinsPlayed == 0) return 0.0;
		return static_cast<double>(totalFreeSpinsPlayed) / static_cast<double>(freeSpinHits);
	}

	//double calculateAverageTumbleFrequency() const {
	//	long long totalTumbles = 0;
	//	long long totalOccurrences = 0;
	//	for (const auto& pair : tumbleFreq) {
	//		totalTumbles += pair.first * pair.second;
	//		totalOccurrences += pair.second;
	//	}
	//	if (totalOccurrences == 0) {
	//		return 0.0;
	//	}
	//	return static_cast<double>(totalTumbles) / totalOccurrences;
	//}

	double calculateAverageFrequency(std::unordered_map<int, long long> freqMap) const {
		long long totalHits = 0;
		long long totalOccurrences = 0;
		for (const auto& pair : freqMap) {
			totalHits += pair.first * pair.second;
			totalOccurrences += pair.second;
		}
		if (totalOccurrences == 0) {
			return 0.0;
		}
		return static_cast<double>(totalHits) / totalOccurrences;
	}

	/*double calculateAverageFreeSpins() const {
		long long totalFreeSpins = 0;
		long long totalOccurrences = 0;
		for (const auto& pair : freeSpinsFreq) {
			totalFreeSpins += pair.first * pair.second;
			totalOccurrences += pair.second;
		}
		if (totalOccurrences == 0) {
			return 0.0;
		}
		return static_cast<double>(totalFreeSpins) / totalOccurrences;
	}*/

	void completeWager(const std::vector<double>& pays) {
		std::lock_guard<std::mutex> lock(statsMutex);
		for (size_t i = 0; i < pays.size(); i++) {
			payVector[i] += pays[i];
			payFrequencies[i][pays[i]]++;
		}
		if (pays[0] > 0) {
			baseGameHits++;
		}
		// Track if total payout is >= cost per spin
		double totalPay = pays.back();
		if (totalPay >= costPerSpin) {
			breakEvenOrBetterSpins++;
		}
		lastPay = pays;
	}

	//void trackFeatureActivation(const std::string& featureName) {
	//    std::lock_guard<std::mutex> lock(statsMutex);
	//    auto it = std::find(featureNames.begin(), featureNames.end(), featureName);
	//    if (it != featureNames.end()) {
	//        size_t index = std::distance(featureNames.begin(), it);
	//        featureHits[index]++;
	//    }
	//}

	void trackFeatureActivation(const std::string& featureName) {
		std::lock_guard<std::mutex> lock(statsMutex);
		featureHits[featureName]++; // Increment the count for the feature
	}

	double calculateStandardDeviation(const std::vector<double>& pays) const {
		if (pays.empty()) return 0.0;
		double mean = std::accumulate(pays.begin(), pays.end(), 0.0) / pays.size();
		double variance = 0.0;
		for (auto pay : pays) {
			variance += std::pow(pay - mean, 2);
		}
		variance /= pays.size();
		return std::sqrt(variance);
	}

	void calculateStandardDeviations() {
		std::lock_guard<std::mutex> lock(statsMutex);
		standardDeviations.clear();
		standardDeviations.resize(payFrequencies.size(), 0.0);

		for (size_t i = 0; i < payFrequencies.size(); ++i) {
			double mean = 0.0;
			double variance = 0.0;
			double totalWeight = 0.0;

			for (const auto& pair : payFrequencies[i]) {
				mean += pair.first * pair.second;
				totalWeight += pair.second;
			}
			mean /= totalWeight;

			for (const auto& pair : payFrequencies[i]) {
				variance += pair.second * std::pow(pair.first - mean, 2);
			}
			variance /= totalWeight;

			standardDeviations[i] = std::sqrt(variance);
		}
	}

	void aggregate(const Stats& other) {
		std::lock_guard<std::mutex> lock(statsMutex);

		numIterations += other.numIterations;
		totalWin += other.totalWin;
		baseGameHits += other.baseGameHits;
		breakEvenOrBetterSpins += other.breakEvenOrBetterSpins;

		for (size_t i = 0; i < payVector.size(); ++i) {
			payVector[i] += other.payVector[i];
			for (const auto& freqPair : other.payFrequencies[i]) {
				payFrequencies[i][freqPair.first] += freqPair.second;
			}
		}

		// Aggregate featureHits
		for (const auto& pair : other.featureHits) {
			featureHits[pair.first] += pair.second;
		}

		for (size_t i = 0; i < baseSymHits.size(); ++i) {
			for (size_t j = 0; j < baseSymHits[i].size(); ++j) {
				baseSymHits[i][j] += other.baseSymHits[i][j];
				baseSymPays[i][j] += other.baseSymPays[i][j];
				freeSymHits[i][j] += other.freeSymHits[i][j];
				freeSymPays[i][j] += other.freeSymPays[i][j];
			}
		}

		for (const auto& pair : other.scatterHits) {
			scatterHits[pair.first] += pair.second;
		}

		for (const auto& pair : other.tumbleFreq) {
			tumbleFreq[pair.first] += pair.second;
		}
		for (const auto& pair : other.tumbleFreqFree) {
			tumbleFreqFree[pair.first] += pair.second;
		}

		for (const auto& pair : other.multFreq) {
			multFreq[pair.first] += pair.second;
		}
		for (const auto& pair : other.multFreqFree) {
			multFreqFree[pair.first] += pair.second;
		}
		for (const auto& outerPair : other.multFreqFreeByInit) {
			int initMult = outerPair.first;
			for (const auto& innerPair : outerPair.second) {
				multFreqFreeByInit[initMult][innerPair.first] += innerPair.second;
			}
		}

		for (const auto& pair : other.freeSpinsFreq) {
			freeSpinsFreq[pair.first] += pair.second;
		}

		// Aggregate boost tracking for free games
		for (const auto& pair : other.boostFreqFree) {
			boostFreqFree[pair.first] += pair.second;
		}
		standardBoostsFree += other.standardBoostsFree;
		superboostsFree += other.superboostsFree;
		totalMultFromStandardBoostsFree += other.totalMultFromStandardBoostsFree;
		totalMultFromSuperboostsFree += other.totalMultFromSuperboostsFree;

		// Aggregate win tracking with superboosts
		winsWithSuperboostBase += other.winsWithSuperboostBase;
		winsWithSuperboostFree += other.winsWithSuperboostFree;
		totalWinsBase += other.totalWinsBase;
		totalWinsFree += other.totalWinsFree;

		// Aggregate free-spin hit counters
		totalFreeSpinsPlayed += other.totalFreeSpinsPlayed;
		freeSpinHits += other.freeSpinHits;

		moneyEntry.first += other.moneyEntry.first;
		moneyEntry.second += other.moneyEntry.second;


		totalWins += other.totalWins;
		totalWinnings += other.totalWinnings;
	}

	double getLastSpinPayout() const {
		if (lastPay.empty()) return 0.0;
		return lastPay.back();
	}

	void recordWin(double amount) {
		std::lock_guard<std::mutex> lock(statsMutex);
		totalWins++;
		totalWinnings += amount;
	}

	void outputData(std::ofstream& file) const {
		file << "RTP and Standard Deviation Breakdown\n";
		file << "Name\tRTP\tStDev\n";

		for (size_t i = 0; i < rtpHeaders.size(); ++i) {
			double rtp = payVector[i] / (numIterations * costPerSpin);
			double stDev = standardDeviations[i];
			file << rtpHeaders[i] << '\t' << std::setprecision(6) << rtp << '\t' << std::setprecision(4) << stDev << '\n';
		}
		file << "----------------------------------------\n";

		file << "Iterations\t" << numIterations << '\n';
		file << "Total Pay\t" << payVector[3] << '\n';

		// Output break-even or better rate
		file << "Break-Even or Better Spins\t" << breakEvenOrBetterSpins << '\n';
		if (numIterations > 0) {
			double breakEvenRate = static_cast<double>(numIterations) / static_cast<double>(breakEvenOrBetterSpins);
			file << "Break-Even or Better Rate\t1 in " << std::setprecision(6) << "\t" << breakEvenRate << '\n';
		}
		file << "----------------------------------------\n";

		// Sort featureHits by name as featureHitsOrdered
		file << "Feature Hits\n";

		file << "Feature\tHits\tHit Rate\n";

		// Copy map to a vector for sorting
		std::vector<std::pair<std::string, long long>> sortedFeatures(featureHits.begin(), featureHits.end());

		// Sort by hits in descending order
		std::sort(sortedFeatures.begin(), sortedFeatures.end(),
			[](const auto& a, const auto& b) {
				return a.second > b.second; // Compare feature hit counts
			});

		// Output sorted results
		for (const auto& pair : sortedFeatures) {
			long long hits = pair.second;
			double hitRate = (hits > 0) ? numIterations / static_cast<double>(hits) : 0.0;
			file << pair.first << '\t' << hits << '\t' << std::setprecision(8) << hitRate << '\n';
		}

		file << "----------------------------------------\n";

		file << "Base Hits\n";
		file << "Symbol";
		for (size_t i = 0; i < baseSymHits[0].size(); ++i) {
			file << '\t' << i + 1;
		}
		file << '\n';
		for (size_t i = 0; i < baseSymHits.size(); ++i) {
			file << symbolStructure.getSymbols()[i];
			for (const auto& hits : baseSymHits[i]) {
				file << '\t' << hits;
			}
			file << '\n';
		}

		file << "----------------------------------------\n";

		file << "Free Hits\n";
		file << "Symbol";
		for (size_t i = 0; i < freeSymHits[0].size(); ++i) {
			file << '\t' << i + 1;
		}
		file << '\n';
		for (size_t i = 0; i < freeSymHits.size(); ++i) {
			file << symbolStructure.getSymbols()[i];
			for (const auto& hits : freeSymHits[i]) {
				file << '\t' << hits;
			}
			file << '\n';
		}

		file << "----------------------------------------\n";

		file << "Free Pays\n";
		for (size_t i = 0; i < freeSymPays[0].size(); ++i) {
			file << '\t' << i + 1;
		}
		file << '\n';
		for (size_t i = 0; i < freeSymPays.size(); ++i) {
			file << symbolStructure.getSymbols()[i];
			for (const auto& pays : freeSymPays[i]) {
				file << '\t' << pays;
			}
			file << '\n';
		}

		file << "----------------------------------------\n";
		file << "Average Free Spins: " << '\t' << calculateAverageFrequency(freeSpinsFreq) << '\n';
		file << "----------------------------------------\n";
	
		file << "Average Tumbles Base: " << '\t' << calculateAverageFrequency(tumbleFreq) << '\n';
		file << "----------------------------------------\n";
		file << "Average Tumbles Free: " << '\t' << calculateAverageFrequency(tumbleFreqFree) << '\n';
		file << "----------------------------------------\n";
		file << "Tumble Frequencies Base\n";
		writeSortedFrequency(file, "Number Tumble", "Frequency", tumbleFreq);
		// Individual free-spin hit rate output
		file << "----------------------------------------\n";
		file << "Individual Free-Spin Hit Rate:\t" << std::setprecision(6) << (getFreeSpinHitRate()) << '\n';
		file << "----------------------------------------\n";
		file << "Multiplier Hit Rate: " << '\t' << calculateMultiplierHitRate() << '\n';
		file << "Average Final Multiplier: " << '\t' << calculateAverageFrequency(multFreq) << '\n';
		file << "Final Multiplier Frequencies\n";
		writeSortedFrequency(file, "Multiplier", "Frequency", multFreq);

		file << "----------------------------------------\n";
		file << "Average Final Multiplier Free Spins: " << '\t' << calculateAverageFrequency(multFreqFree) << '\n';
		file << "Final Multiplier Frequencies Free Spins\n";
		writeSortedFrequency(file, "Multiplier", "Frequency", multFreqFree);
		file << "----------------------------------------\n";

		// Boost tracking for free games
		file << "Boost Statistics (Free Games)\n";
		file << "Standard Boosts (+1):\t" << standardBoostsFree
			 << "\t(Total Mult: " << totalMultFromStandardBoostsFree << ")\n";
		file << "Superboosts (+10):\t" << superboostsFree
			 << "\t(Total Mult: " << totalMultFromSuperboostsFree << ")\n";

		long long totalBoostsFree = standardBoostsFree + superboostsFree;
		if (totalBoostsFree > 0) {
			double superboostPercentage = (static_cast<double>(superboostsFree) / totalBoostsFree) * 100.0;
			file << "Superboost %:\t" << std::setprecision(4) << superboostPercentage << "%\n";

			double avgMultPerBoost = static_cast<double>(totalMultFromStandardBoostsFree + totalMultFromSuperboostsFree) / totalBoostsFree;
			file << "Avg Mult per Boost:\t" << std::setprecision(4) << avgMultPerBoost << "\n";
		}

		file << "\nBoost Level Frequencies (Free Games)\n";
		writeSortedFrequency(file, "Boost Level", "Frequency", boostFreqFree);
		file << "----------------------------------------\n";

		// Win statistics with superboosts
		file << "Wins with Superboosts\n";
		file << "Base Game Wins:\t" << totalWinsBase << "\n";
		file << "Base Game Wins with Superboost:\t" << winsWithSuperboostBase;
		if (totalWinsBase > 0) {
			double basePercentage = (static_cast<double>(winsWithSuperboostBase) / totalWinsBase) * 100.0;
			file << "\t(" << std::setprecision(4) << basePercentage << "%)\n";
		} else {
			file << "\t(0%)\n";
		}

		file << "Free Game Wins:\t" << totalWinsFree << "\n";
		file << "Free Game Wins with Superboost:\t" << winsWithSuperboostFree;
		if (totalWinsFree > 0) {
			double freePercentage = (static_cast<double>(winsWithSuperboostFree) / totalWinsFree) * 100.0;
			file << "\t(" << std::setprecision(4) << freePercentage << "%)\n";
		} else {
			file << "\t(0%)\n";
		}

		
		/*file << "----------------------------------------\n";
		file << "Final Multiplier Frequencies Free Spins (split by initial multiplier)\n"; */
		
	}

	void printFrequencyTableToFile(const std::string& categoryName, const std::unordered_map<double, long long>& frequencyMap) const {
		std::string filename = "pay_frequency_" + categoryName + ".txt";
		std::ofstream file(filename);
		if (!file.is_open()) {
			std::cerr << "Failed to open " << filename << std::endl;
			return;
		}

		std::vector<std::pair<double, long long>> freqVector(frequencyMap.begin(), frequencyMap.end());
		std::sort(freqVector.begin(), freqVector.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

		file << "Pay\tFrequency\n";
		for (const auto& pair : freqVector) {
			file << pair.first << "\t" << pair.second << "\n";
		}

		file.close();
	}

	void printFrequencyTables() const {
		for (size_t i = 0; i < payFrequencies.size(); ++i) {
			printFrequencyTableToFile(rtpHeaders[i], payFrequencies[i]);
		}
	}

	int getTumbleCount() const {
		return std::accumulate(tumbleFreq.begin(), tumbleFreq.end(), 0,
			[](int sum, const auto& pair) { return sum + pair.second; });
	}

	double getFreeSpinPayout() const { //change depending on payVector
		// Calculate free spin pays
		double freeSpinPayout = 0.0;
		//for (size_t i = 2; i < 7; ++i) { // Change to the range of indices that represent free spins
		//	freeSpinPayout += payVector[i];
		//}
		freeSpinPayout = payVector[3]; // Assuming index 3 corresponds to FREE_TOTAL
		return freeSpinPayout;
	}


	void trackMoneyEntry(double amount) {
		std::lock_guard<std::mutex> lock(statsMutex);
		moneyEntry.first++;
		moneyEntry.second += amount;
	}

};
