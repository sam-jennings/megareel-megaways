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

SimulationMode simulationMode = SIMULATE_MODE; // SIMULATE_MODE, LOG_MODE, REPLAY_MODE, PLAYER_MODE, CSV_MODE



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

    long long numberOfSpins = 5000000000LL; //logging: 100000 



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

    // Call handleLoggingMode to initialize logging/replay if the simulation mode requires it
    bool loggingInitialized = RandomLogGenerator::handleLoggingMode(randomLogFileName, gameDetailsFileName);

    if (!loggingInitialized && (simulationMode == LOG_MODE || simulationMode == REPLAY_MODE)) {
        cerr << "Error initializing logging or replay mode!" << endl;
        return 1;  // Exit if there was an error initializing logging or replay mode
    }

    // Depending on the selected mode, execute the corresponding simulation
    if (simulationMode == SIMULATE_MODE || simulationMode == LOG_MODE || simulationMode == REPLAY_MODE) {

        int numThreads;
        if (simulationMode == SIMULATE_MODE)
            numThreads = 20;
        else
            numThreads = 1;  // Logging/replay requires sequential execution

        double numSpinsPerThread = (double)numberOfSpins / numThreads;

        // Create threads and per-thread stats
        std::vector<std::thread> threads;
        std::vector<std::shared_ptr<Stats>> threadStats;

        std::atomic<long long> completedSpins{0};

        for (int i = 0; i < numThreads; ++i) {
            auto stats = std::make_shared<Stats>(symbolStructure, rtpHeaders, costPerSpin);
            stats->setNumIterations(numSpinsPerThread); // Set the number of iterations for each thread
            threadStats.emplace_back(stats);
            threads.emplace_back([config, &symbolStructure, &threadStats, i, numSpinsPerThread, &completedSpins]() {
                GameInstance instance(config, symbolStructure, *threadStats[i]);
                instance.playBaseGame(numSpinsPerThread, completedSpins);
                });
        }

        // Progress monitor thread
        std::atomic<bool> simDone{false};
        std::thread progressThread([&completedSpins, &simDone, &timer, numberOfSpins]() {
            auto formatTime = [](double secs) -> std::string {
                int h = (int)(secs / 3600);
                int m = (int)((secs - h * 3600) / 60);
                int s = (int)(secs) % 60;
                char buf[32];
                if (h > 0)
                    std::snprintf(buf, sizeof(buf), "%dh %dm %ds", h, m, s);
                else if (m > 0)
                    std::snprintf(buf, sizeof(buf), "%dm %ds", m, s);
                else
                    std::snprintf(buf, sizeof(buf), "%ds", s);
                return std::string(buf);
            };

            while (!simDone.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                long long done = completedSpins.load(std::memory_order_relaxed);
                double elapsed = timer.stop();
                double pct = (double)done / (double)numberOfSpins * 100.0;
                double spinsPerSec = elapsed > 0.0 ? done / elapsed : 0.0;
                double remaining = spinsPerSec > 0.0 ? (numberOfSpins - done) / spinsPerSec : 0.0;
                char buf[256];
                std::snprintf(buf, sizeof(buf),
                    "\r  Progress: %lld / %lld  (%.1f%%)  |  Elapsed: %s  |  ETA: ~%s  |  %.1fM spins/s  ",
                    done, numberOfSpins, pct,
                    formatTime(elapsed).c_str(),
                    formatTime(remaining).c_str(),
                    spinsPerSec / 1e6);
                std::cout << buf << std::flush;
            }
            std::cout << std::endl;
        });

        // Join worker threads
        for (auto& t : threads) {
            t.join();
        }
        simDone.store(true);
        progressThread.join();

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

        const long long defaultSpins = 1000000LL; //1000000LL
        std::string csvFileName = outputFileBase + "_simulation.csv";

        std::ostringstream csvData;
        csvData << "GAME NAME: " << gameInfo[0] << "\n";
        csvData << "GAME VERSION: " << userGameVersion << "\n\n";
        csvData << "RTP SIMULATION RESULTS\n\n";
        csvData << "PLAYER 1 RTP SIMULATION RESULTS\n";
        csvData << "SPINID,TOTAL STAKE,BALANCE,BASE GAME,FREE SPINS,TOTALWIN,TOTAL WINS,REELSET_ID,CASCADE_COUNT\n";

        long long totalWager = 0;
        double balance = 500.0, totalWins = 0.0; // Starting balance

        for (long long i = 0; i < defaultSpins; ++i) {
            double spinWin = 0.0, freeSpinWin = 0.0, baseGameWin = 0.0, modCost = (costPerSpin / 100);
            int cascadeCount, reelsetId = -1; // You'll need a real getter here


            Stats stats(symbolStructure, rtpHeaders, costPerSpin);
            GameInstance gameInstance(config, symbolStructure, stats);

            std::atomic<long long> csvDummy{0};
            gameInstance.playBaseGame(1, csvDummy); // Simulate a single spin

            spinWin = stats.getLastSpinPayout() / 100;
            freeSpinWin = stats.getFreeSpinPayout() / 100;
            baseGameWin = spinWin - freeSpinWin;
            totalWins += spinWin;

            // Assuming GameInstance has a method to get reelset ID
            reelsetId = gameInstance.getLastReelSetID();
			cascadeCount = gameInstance.getBaseTumbleCount(); 

            totalWager += modCost;
            balance += spinWin - modCost;

            csvData << i << ',' << (i + 1) * modCost << ',' << std::fixed << std::setprecision(2) << balance << ','
                << baseGameWin << ',' << freeSpinWin << ',' << spinWin << ',' << totalWins << ',' << reelsetId << ',' << cascadeCount << '\n';
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