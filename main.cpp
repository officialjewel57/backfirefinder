#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <conio.h>

class SeedRandom {
    static constexpr int WIDTH = 256;
    static constexpr int CHUNKS = 6;
    static constexpr uint64_t START_DENOM = 1ULL << 48;
    static constexpr uint64_t SIGNIFICANCE = 1ULL << 52;
    static constexpr uint64_t OVERFLOW_ = 1ULL << 53;

    int S[WIDTH];
    int i = 0, j = 0;

    uint64_t g(int count) {
        uint64_t r = 0;
        while (count--) {
            int t = S[i = (i + 1) & 255];
            j = (j + t) & 255;
            S[i] = S[j];
            S[j] = t;
            r = r * WIDTH + S[(S[i] + S[j]) & 255];
        }
        return r;
    }

public:
    explicit SeedRandom(const std::string& seed) {
        std::vector<int> key;
        int smear = 0;
        for (size_t k = 0; k < seed.size(); k++) {
            size_t idx = k & 255;
            if (idx >= key.size()) key.resize(idx + 1, 0);
            smear ^= key[idx] * 19;
            key[idx] = 255 & (smear + (unsigned char)seed[k]);
        }
        if (key.empty()) key.push_back(0);

        for (int k = 0; k < WIDTH; k++) S[k] = k;
        int jj = 0;
        for (int k = 0; k < WIDTH; k++) {
            int t = S[k];
            jj = 255 & (jj + key[k % key.size()] + t);
            S[k] = S[jj];
            S[jj] = t;
        }
        i = j = 0;
        g(WIDTH);
    }

    double next() {
        uint64_t n = g(CHUNKS);
        double d = (double)START_DENOM;
        uint64_t x = 0;
        while (n < SIGNIFICANCE) {
            n = (n + x) * WIDTH;
            d *= WIDTH;
            x = g(1);
        }
        while (n >= OVERFLOW_) {
            n /= 2;
            d /= 2;
            x >>= 1;
        }
        return (double)(n + x) / d;
    }
};

static std::mt19937 rng{std::random_device{}()};

std::string makeSeed() {
    static const std::string chars = "abcdefghijklmnopqrstuvwxyz";
    std::uniform_int_distribution<int> dist(0, 25);
    std::string str;
    for (int i = 0; i < 5; i++) str += chars[dist(rng)];
    return str;
}

std::string nextString(const std::string& str) {
    std::string chars = str;
    int i = (int)chars.size() - 1;
    while (i >= 0) {
        if (chars[i] == 'z') {
            chars[i] = 'a';
            i--;
        } else {
            chars[i]++;
            return chars;
        }
    }
    return "undefined";
}

long long a = 0;

std::vector<std::string> pickleSeedSearch(long long numSeeds, long long currentSpellCount,
                                          long long maxSpellCount, long long count, std::ofstream& out) {
    std::vector<std::string> list;
    auto start = std::chrono::steady_clock::now();

    for (long long j = 0; j <= numSeeds; j++) {
        std::string seed = makeSeed();

        if (std::fmod((double)j, std::sqrt((double)numSeeds)) == 0) {
            std::cout << j << "/" << numSeeds << ".    " << std::round(((double)j / numSeeds) * 10000) / 100 << "%\n";
        }

        for (long long i = currentSpellCount; i <= maxSpellCount; i++) {
            SeedRandom sr(seed + "/" + std::to_string(i));
            double b = sr.next();

            if (b > 0.125 && b < 0.25) a++;
            if (b > 0.25 || b < 0.125) a = 0;
            if (i - a < 0) i = a;

            if (a >= count) {
                std::string line = std::to_string(a) + ", " + std::to_string(i - a) + ", " + seed;
                std::cout << line << "\n";
                out << line << "\n";
                out.flush();
                list.push_back(line);
            }
        }
    }

    std::chrono::duration<double> el = std::chrono::steady_clock::now() - start;
    std::cout << "Time elapsed: " << el.count() << "s\n";
    return list;
}

std::string indexToSeed(long long idx) {
    std::string s(5, 'a');
    for (int p = 4; p >= 0; p--) {
        s[p] = 'a' + (idx % 26);
        idx /= 26;
    }
    return s;
}

void backfireFinder(long long numSeeds, long long currentSpellCount,
                    long long maxSpellCount, long long count,
                    std::ofstream& out, unsigned numThreads = 0) {
    if (numThreads == 0)
        numThreads = std::max(1u, std::thread::hardware_concurrency());

    const long long total = std::min(numSeeds, 11881376LL);
    const long long CHUNK = 1000;           // seeds handed out per grab

    std::atomic<long long> nextChunk{0};
    std::atomic<long long> done{0};
    std::mutex outMutex;

    auto start = std::chrono::steady_clock::now();

    auto worker = [&]() {
        while (true) {
            long long begin = nextChunk.fetch_add(CHUNK);
            if (begin >= total) break;
            long long end = std::min(begin + CHUNK, total);

            for (long long j = begin; j < end; j++) {
                std::string seed = indexToSeed(j);
                long long a = 0;            // local streak counter

                for (long long i = currentSpellCount; i <= maxSpellCount; i++) {
                    SeedRandom sr(seed + "/" + std::to_string(i));
                    double b = sr.next();

                    if (b > 0.85) a++;
                    else a = 0;

                    if (a >= count) {
                        std::string line = std::to_string(a) + ", " +
                                           std::to_string((i - a) + 1) + ", " + seed;
                        std::lock_guard<std::mutex> lock(outMutex);
                        std::cout << line << "\n";
                        out << line << "\n";
                        out.flush();
                    }
                }
            }
            done += (end - begin);
        }
    };

    std::vector<std::thread> threads;
    for (unsigned t = 0; t < numThreads; t++) threads.emplace_back(worker);

    // progress reporting from the main thread
    while (done < total) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        long long d = done.load();
        std::lock_guard<std::mutex> lock(outMutex);
        std::cout << "\033[1;31m" << d << "/" << total << ".    "
                  << std::round(((double)d / total) * 10000) / 100 << "%\033[0m\n";
    }

    for (auto& th : threads) th.join();

    std::chrono::duration<double> el = std::chrono::steady_clock::now() - start;
    std::cout << "\033[1;31mTime elapsed: " << el.count() << "s\033[0m\n";
}

void successFinder(long long numSeeds, long long currentSpellCount,
                    long long maxSpellCount, long long count,
                    std::ofstream& out, unsigned numThreads = 0) {
    if (numThreads == 0)
        numThreads = std::max(1u, std::thread::hardware_concurrency());

    const long long total = std::min(numSeeds, 11881376LL);
    const long long CHUNK = 1000;           // seeds handed out per grab

    std::atomic<long long> nextChunk{0};
    std::atomic<long long> done{0};
    std::mutex outMutex;

    auto start = std::chrono::steady_clock::now();

    auto worker = [&]() {
        while (true) {
            long long begin = nextChunk.fetch_add(CHUNK);
            if (begin >= total) break;
            long long end = std::min(begin + CHUNK, total);

            for (long long j = begin; j < end; j++) {
                std::string seed = indexToSeed(j);
                long long a = 0;            // local streak counter

                for (long long i = currentSpellCount; i <= maxSpellCount; i++) {
                    SeedRandom sr(seed + "/" + std::to_string(i));
                    double b = sr.next();

                    if (b < 0.85) a++;
                    else a = 0;

                    if (a >= count) {
                        std::string line = std::to_string(a) + ", " +
                                           std::to_string((i - a) + 1) + ", " + seed;
                        std::lock_guard<std::mutex> lock(outMutex);
                        std::cout << line << "\n";
                        out << line << "\n";
                        out.flush();
                    }
                }
            }
            done += (end - begin);
        }
    };

    std::vector<std::thread> threads;
    for (unsigned t = 0; t < numThreads; t++) threads.emplace_back(worker);

    // progress reporting from the main thread
    while (done < total) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        long long d = done.load();
        std::lock_guard<std::mutex> lock(outMutex);
        std::cout << "\033[1;31m" << d << "/" << total << ".    "
                  << std::round(((double)d / total) * 10000) / 100 << "%\033[0m\n";
    }

    for (auto& th : threads) th.join();

    std::chrono::duration<double> el = std::chrono::steady_clock::now() - start;
    std::cout << "\033[1;31mTime elapsed: " << el.count() << "s\033[0m\n";
}

int main() {
    std::ofstream out("backfireStrings.txt", std::ios::app);   // app = append instead of overwrite
    if (!out) {
        std::cerr << "Could not open results.txt\n";
        return 1;
    }
    int currentSpellCount = 0;
    int maximumSpellCount = 0;
    int backfireCount = 9;
    int seedCount = 11881376;
    bool exit = false;
    std::cout << "Enter \033[1;34mcurrent spell count\033[0m \033[1;31m(as an integer)\033[0m" << std::endl;
    std::cin >> currentSpellCount;
    std::cout << "Enter \033[1;36mmaximum spell count\033[0m (to make a range of spells; \033[1;31mas an integer\033[0m)" << std::endl;
    std::cin >> maximumSpellCount; 
    std::cout << "How many seeds to search? (default is 26^5 or 11,881,376 seeds; \033[1;31mas an integer\033[0m)" << std::endl;
    std::cin >> seedCount;
    std::cout << "Backfires in a row? defaulted to 9 (\033[1;31mas an integer\033[0m)" << std::endl;
    std::cin >> backfireCount;
    std::cout << "\n";
    std::cout << "===================================\n";
    std::cout << "\n";
    backfireFinder(seedCount, currentSpellCount, maximumSpellCount, backfireCount, out);
    std::cout << "Done. Check 'backfireStrings.txt' to see the results, press any key to exit." << std::endl;
    _getch();
    return 0;
}
