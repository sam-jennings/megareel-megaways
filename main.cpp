#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <numeric>
#include <unordered_map>
#include <random>
#include <iomanip> // For std::setw and std::left
#include "Stats.h" 
#include "GameInstance.h"

using namespace std;

#include <chrono> // For time measurements
#include <thread> // For multithreading
#include <future> // For std::promise and std::future

class Timer {
private:
    std::chrono::high_resolution_clock::time_point start_time;
public:
    void start() {
        start_time = std::chrono::high_resolution_clock::now();
    }

    double stop() {
        auto end_time = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> duration = end_time - start_time;
        return duration.count();
    }
};

enum SimulationMode {
    EXACT_MODE,
    RANDOM_MODE,
    PLAYER_MODE,
    CSV_MODE
};

LogMode logMode = LOGGING; // NO_LOGGING, LOGGING, REPLAY
SimulationMode simulationMode = RANDOM_MODE; // RANDOM_MODE;



int main() {
    // Create a timer instance
    Timer timer;
    timer.start();

    //GameConfig gameConfig("config.json");
    // Initialize configuration
    std::shared_ptr<GameConfig> config = std::make_shared<GameConfig>("config.json");

    // Parse RTP headers
    const std::vector<std::string>& rtpHeaders = config->getRTPHeaders();

    // Parse feature names
   // const std::vector<std::string>& featureNames = config->getFeatureNames();

    // Initialize symbol structure
    SymbolStructure symbolStructure = config->parseSymbolStructure();

    //get gameInfo
    std::vector<std::string> gameInfo = config->getGameInfo();
    const double costPerSpin = config->parseVar<double>("cost");

    long long numberOfSpins = 100000LL; //logging: 100000 



    std::string outputFileBase = gameInfo[0] + "_RTP" + gameInfo[1] + "_" + gameInfo[2];
    std::string outputFileName = outputFileBase + "_output.txt";
    std::string randomLogFileName = outputFileBase + "_randomLog.txt";
    std::string gameDetailsFileName = outputFileBase + "_gameDetails.txt";

    Stats finalStats(symbolStructure, rtpHeaders, costPerSpin);



    // Open output file
    ofstream outputFile(outputFileName);
    if (!outputFile.is_open()) {
        cerr << "Failed to open output file." << endl; //good to check before running simulation
    }

    // Call handleLoggingMode to initialize the logging mode, whether LOGGING, REPLAY, or NO_LOGGING
    bool loggingInitialized = RandomLogGenerator::handleLoggingMode(logMode, randomLogFileName, gameDetailsFileName);

    if (!loggingInitialized && logMode != NO_LOGGING) {
        cerr << "Error initializing logging or replay mode!" << endl;
        return 1;  // Exit if there was an error initializing logging or replay mode
    }

    // Depending on the selected mode, execute the corresponding simulation
    if (simulationMode == RANDOM_MODE) { // Simulate number of spins

        int numThreads;
        if (logMode == NO_LOGGING)
            numThreads = 20; //20
        else
            numThreads = 1;

        double numSpinsPerThread = (double)numberOfSpins / numThreads;

        // Create threads and per-thread stats
        std::vector<std::thread> threads;
        std::vector<std::shared_ptr<Stats>> threadStats;

        for (int i = 0; i < numThreads; ++i) {
            auto stats = std::make_shared<Stats>(symbolStructure, rtpHeaders, costPerSpin);
            stats->setNumIterations(numSpinsPerThread); // Set the number of iterations for each thread
            threadStats.emplace_back(stats);
            threads.emplace_back([config, &symbolStructure, &threadStats, i, numSpinsPerThread]() {
                GameInstance instance(config, symbolStructure, *threadStats[i]);
                instance.playBaseGame(numSpinsPerThread);
                });
        }

        // Join threads
        for (auto& t : threads) {
            t.join();
        }

        // Aggregate stats
        for (const auto& s : threadStats) {
            finalStats.aggregate(*s); // Dereference the shared_ptr to pass the Stats object
        }
        finalStats.calculateStandardDeviations();
        finalStats.outputData(outputFile);
        finalStats.printFrequencyTables();

    }
    else if (simulationMode == PLAYER_MODE) {
        int N = 10000; // Number of players 100000
        int X = 2000; // Starting credits (enough for 100 spins)
        int Y = 4000; // Target credits (enough for 200 spins)
        int successfulPlayers = 0;

        for (int i = 0; i < N; ++i) {
            // Initialize Stats for each player (if stats aggregation is not needed across players)
            Stats stats(symbolStructure, rtpHeaders, costPerSpin);

            // Initialize PlayerSimulation with the shared config, symbolStructure, and unique Stats instance
            PlayerSimulation sim(X, Y, config, symbolStructure, stats);

            if (sim.simulate()) {
                successfulPlayers++;
            }
        }

        double successPercentage = (double)successfulPlayers / N * 100.0;
        cout << "Percentage of players reaching target credits: " << successPercentage << "%\n";
        return 0;
    }
    else if (simulationMode == CSV_MODE) {
        std::string userGameVersion;
        std::cout << "Enter the game version : ";
        std::getline(std::cin, userGameVersion);

        const long long defaultSpins = 1000000LL;
        std::string csvFileName = outputFileBase + "_simulation.csv";

        std::ostringstream csvData;
        csvData << "GAME NAME: " << gameInfo[0] << "\n";
        csvData << "GAME VERSION: " << userGameVersion << "\n\n";
        csvData << "RTP SIMULATION RESULTS\n\n";
        csvData << "PLAYER 1 RTP SIMULATION RESULTS\n";
        csvData << "SPINID,TOTAL STAKE,BALANCE,BASE GAME,FREE SPINS,TOTALWIN,TOTAL WINS,REELSET_ID\n";

        long long totalWager = 0;
        double balance = 500.0, totalWins = 0.0; // Starting balance

        for (long long i = 0; i < defaultSpins; ++i) {
            double spinWin = 0.0, freeSpinWin = 0.0, baseGameWin = 0.0, modCost = (costPerSpin / 100);
            int reelsetId = -1; // You'll need a real getter here


            Stats stats(symbolStructure, rtpHeaders, costPerSpin);
            GameInstance gameInstance(config, symbolStructure, stats);

            gameInstance.playBaseGame(1); // Simulate a single spin

            spinWin = stats.getLastSpinPayout() / 100;
            freeSpinWin = stats.getFreeSpinPayout() / 100;
            baseGameWin = spinWin - freeSpinWin;
            totalWins += spinWin;

            // Assuming GameInstance has a method to get reelset ID
            reelsetId = gameInstance.getLastReelSetID(); // <-- implement this if not present

            totalWager += modCost;
            balance += spinWin - modCost;

            csvData << i << ',' << (i + 1) * modCost << ',' << std::fixed << std::setprecision(2) << balance << ','
                << baseGameWin << ',' << freeSpinWin << ',' << spinWin << ',' << totalWins << ',' << reelsetId << '\n';
        }

        std::ofstream csvFile(csvFileName);
        if (!csvFile.is_open()) {
            std::cerr << "Failed to open CSV output file: " << csvFileName << std::endl;
            return 1;
        }
        csvFile << csvData.str();
        csvFile.close();

        std::cout << "CSV simulation completed. Output file: " << csvFileName << std::endl;
    }  else {
        cerr << "Invalid simulation mode" << endl;
        return 1;
    }



    // Stop the timer
    double elapsed_time = timer.stop();

    // Print the elapsed time to the output file
    outputFile << "Elapsed time: " << elapsed_time << " seconds" << endl;
    outputFile.close();

    RandomLogGenerator::closeLogs();

    return 0;
}
