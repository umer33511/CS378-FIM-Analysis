#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <map>
#include <string>
#include <algorithm>
#include <chrono>
#include <numeric>
#include <thread>
#include <mutex>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

using namespace std;
using namespace chrono;

using Transaction = vector<string>;
using Database    = vector<Transaction>;
using Itemset     = set<string>;
using FreqMap     = map<Itemset, int>;
using TidList     = unordered_set<int>;
using TidMap      = map<Itemset, TidList>;


// ---------------------------------------------------------------------------
// Memory measurement
// ---------------------------------------------------------------------------

double get_peak_memory_mb() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    return pmc.PeakWorkingSetSize / (1024.0 * 1024.0);
#else
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss / 1024.0;
#endif
}


// ---------------------------------------------------------------------------
// Dataset loader
// ---------------------------------------------------------------------------

Database load_dataset(const string& filename) {
    Database db;
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "ERROR: Cannot open " << filename << "\n";
        return db;
    }
    string line;
    while (getline(file, line)) {
        if (line.empty()) continue;
        Transaction t;
        istringstream iss(line);
        string item;
        while (iss >> item) t.push_back(item);
        if (!t.empty()) db.push_back(t);
    }
    return db;
}


// ---------------------------------------------------------------------------
// Candidate generation with Apriori pruning
// ---------------------------------------------------------------------------

vector<Itemset> generate_candidates(const vector<Itemset>& freq_sets, int k) {
    vector<Itemset> candidates;
    int n = freq_sets.size();
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            Itemset candidate;
            set_union(freq_sets[i].begin(), freq_sets[i].end(),
                      freq_sets[j].begin(), freq_sets[j].end(),
                      inserter(candidate, candidate.begin()));
            if ((int)candidate.size() != k) continue;

            bool valid = true;
            for (auto it = candidate.begin(); it != candidate.end() && valid; ++it) {
                Itemset subset = candidate;
                subset.erase(*it);
                bool found = false;
                for (auto& fs : freq_sets)
                    if (fs == subset) { found = true; break; }
                if (!found) valid = false;
            }
            if (valid) candidates.push_back(candidate);
        }
    }
    sort(candidates.begin(), candidates.end());
    candidates.erase(unique(candidates.begin(), candidates.end()), candidates.end());
    return candidates;
}


// ---------------------------------------------------------------------------
// Baseline Apriori
// ---------------------------------------------------------------------------

pair<int,int> baseline_apriori(const Database& db, double min_sup) {
    int n = db.size();
    int min_count = (int)(min_sup * n);
    FreqMap all_freq;
    int total_candidates = 0;

    map<string, int> item_counts;
    for (auto& t : db)
        for (auto& item : t)
            item_counts[item]++;

    vector<Itemset> freq_sets;
    for (auto& [item, cnt] : item_counts) {
        if (cnt >= min_count) {
            Itemset is = {item};
            all_freq[is] = cnt;
            freq_sets.push_back(is);
        }
    }
    total_candidates += item_counts.size();

    int k = 2;
    while (!freq_sets.empty()) {
        auto candidates = generate_candidates(freq_sets, k);
        total_candidates += candidates.size();
        if (candidates.empty()) break;

        map<Itemset, int> counts;
        for (auto& c : candidates) counts[c] = 0;
        for (auto& t : db) {
            set<string> t_set(t.begin(), t.end());
            for (auto& c : candidates)
                if (includes(t_set.begin(), t_set.end(), c.begin(), c.end()))
                    counts[c]++;
        }

        freq_sets.clear();
        for (auto& [c, cnt] : counts) {
            if (cnt >= min_count) {
                all_freq[c] = cnt;
                freq_sets.push_back(c);
            }
        }
        k++;
    }
    return {(int)all_freq.size(), total_candidates};
}


// ---------------------------------------------------------------------------
// Optimization 1: Tid-List Apriori
// Replaces repeated horizontal database scans with tid-list set intersections.
// sup(X ∪ Y) = |T(X) ∩ T(Y)|, computed once per candidate pair.
// ---------------------------------------------------------------------------

pair<int,int> tidlist_apriori(const Database& db, double min_sup) {
    int n = db.size();
    int min_count = (int)(min_sup * n);
    int total_freq = 0;
    int total_candidates = 0;

    map<string, TidList> item_tids;
    for (int tid = 0; tid < n; tid++)
        for (auto& item : db[tid])
            item_tids[item].insert(tid);

    TidMap freq_tidlists;
    for (auto& [item, tids] : item_tids) {
        if ((int)tids.size() >= min_count) {
            freq_tidlists[{item}] = tids;
            total_freq++;
        }
    }
    total_candidates += item_tids.size();

    int k = 2;
    while (!freq_tidlists.empty()) {
        vector<pair<Itemset, TidList>> freq_vec(freq_tidlists.begin(), freq_tidlists.end());
        TidMap new_freq_tidlists;

        for (int i = 0; i < (int)freq_vec.size(); i++) {
            for (int j = i + 1; j < (int)freq_vec.size(); j++) {
                Itemset candidate;
                set_union(freq_vec[i].first.begin(), freq_vec[i].first.end(),
                          freq_vec[j].first.begin(), freq_vec[j].first.end(),
                          inserter(candidate, candidate.begin()));
                if ((int)candidate.size() != k) continue;

                bool valid = true;
                for (auto it = candidate.begin(); it != candidate.end() && valid; ++it) {
                    Itemset subset = candidate;
                    subset.erase(*it);
                    if (freq_tidlists.find(subset) == freq_tidlists.end())
                        valid = false;
                }
                if (!valid) continue;

                total_candidates++;

                TidList intersection;
                for (int tid : freq_vec[i].second)
                    if (freq_vec[j].second.count(tid))
                        intersection.insert(tid);

                if ((int)intersection.size() >= min_count) {
                    new_freq_tidlists[candidate] = intersection;
                    total_freq++;
                }
            }
        }
        freq_tidlists = new_freq_tidlists;
        k++;
    }
    return {total_freq, total_candidates};
}


// ---------------------------------------------------------------------------
// Optimization 2: Parallel Apriori
// Partitions the database across hardware threads for concurrent support
// counting. Threads share the read-only database with zero copy overhead.
// ---------------------------------------------------------------------------

void count_chunk(const Database& db, int start, int end,
                 const vector<Itemset>& candidates,
                 vector<int>& counts, mutex& mtx) {
    vector<int> local(candidates.size(), 0);
    for (int i = start; i < end; i++) {
        set<string> t_set(db[i].begin(), db[i].end());
        for (int ci = 0; ci < (int)candidates.size(); ci++)
            if (includes(t_set.begin(), t_set.end(),
                         candidates[ci].begin(), candidates[ci].end()))
                local[ci]++;
    }
    lock_guard<mutex> lock(mtx);
    for (int ci = 0; ci < (int)local.size(); ci++)
        counts[ci] += local[ci];
}

pair<int,int> parallel_apriori(const Database& db, double min_sup) {
    int n = db.size();
    int min_count = (int)(min_sup * n);
    FreqMap all_freq;
    int total_candidates = 0;
    int num_threads = max(1u, thread::hardware_concurrency());

    map<string, int> item_counts;
    for (auto& t : db)
        for (auto& item : t)
            item_counts[item]++;

    vector<Itemset> freq_sets;
    for (auto& [item, cnt] : item_counts) {
        if (cnt >= min_count) {
            Itemset is = {item};
            all_freq[is] = cnt;
            freq_sets.push_back(is);
        }
    }
    total_candidates += item_counts.size();

    int k = 2;
    while (!freq_sets.empty()) {
        auto candidates = generate_candidates(freq_sets, k);
        total_candidates += candidates.size();
        if (candidates.empty()) break;

        vector<int> counts(candidates.size(), 0);
        mutex mtx;
        vector<thread> threads;
        int chunk = n / num_threads;

        for (int t = 0; t < num_threads; t++) {
            int start = t * chunk;
            int end   = (t == num_threads - 1) ? n : start + chunk;
            threads.emplace_back(count_chunk, ref(db), start, end,
                                 ref(candidates), ref(counts), ref(mtx));
        }
        for (auto& th : threads) th.join();

        freq_sets.clear();
        for (int ci = 0; ci < (int)candidates.size(); ci++) {
            if (counts[ci] >= min_count) {
                all_freq[candidates[ci]] = counts[ci];
                freq_sets.push_back(candidates[ci]);
            }
        }
        k++;
    }
    return {(int)all_freq.size(), total_candidates};
}


// ---------------------------------------------------------------------------
// FP-Growth
// Pattern-growth approach: builds a compressed FP-tree in two database scans,
// then mines frequent itemsets via recursive conditional pattern bases.
// Generates zero candidates at any point.
// Reference: Liu (2025), Int. J. Housing Sci. Appl., vol. 46, no. 3.
// ---------------------------------------------------------------------------

struct FPNode {
    string item;
    int count;
    FPNode* parent;
    FPNode* link;
    unordered_map<string, FPNode*> children;

    FPNode(string it, int c, FPNode* p)
        : item(move(it)), count(c), parent(p), link(nullptr) {}
};

struct FPTree {
    FPNode* root;
    unordered_map<string, FPNode*> header;
    unordered_map<string, int> freq;
    int min_count;

    explicit FPTree(int mc) : min_count(mc) {
        root = new FPNode("", 0, nullptr);
    }

    void build(const Database& db) {
        for (auto& t : db)
            for (auto& item : t)
                freq[item]++;

        for (auto& t : db) {
            vector<string> filtered;
            for (auto& item : t)
                if (freq.count(item) && freq[item] >= min_count)
                    filtered.push_back(item);
            sort(filtered.begin(), filtered.end(),
                 [&](const string& a, const string& b) {
                     return freq[a] != freq[b] ? freq[a] > freq[b] : a < b;
                 });
            insert(filtered, root);
        }
    }

    void insert(const vector<string>& items, FPNode* node) {
        if (items.empty()) return;
        const string& item = items[0];
        FPNode* child;
        if (node->children.count(item)) {
            child = node->children[item];
            child->count++;
        } else {
            child = new FPNode(item, 1, node);
            node->children[item] = child;
            if (header.count(item)) {
                FPNode* cur = header[item];
                while (cur->link) cur = cur->link;
                cur->link = child;
            } else {
                header[item] = child;
            }
        }
        insert(vector<string>(items.begin() + 1, items.end()), child);
    }

    ~FPTree() { destroy(root); }

    void destroy(FPNode* node) {
        for (auto& [k, c] : node->children) destroy(c);
        delete node;
    }
};

int fp_count = 0;

void fp_mine(FPTree* tree, vector<string> prefix, int min_count) {
    for (auto& [item, node] : tree->header) {
        int support = 0;
        for (FPNode* cur = node; cur; cur = cur->link)
            support += cur->count;
        if (support < min_count) continue;

        vector<string> new_prefix = prefix;
        new_prefix.push_back(item);
        fp_count++;

        Database cond_db;
        for (FPNode* cur = node; cur; cur = cur->link) {
            vector<string> path;
            for (FPNode* p = cur->parent; p && !p->item.empty(); p = p->parent)
                path.push_back(p->item);
            for (int i = 0; i < cur->count; i++)
                if (!path.empty()) cond_db.push_back(path);
        }

        if (!cond_db.empty()) {
            FPTree* cond_tree = new FPTree(min_count);
            cond_tree->build(cond_db);
            if (!cond_tree->header.empty())
                fp_mine(cond_tree, new_prefix, min_count);
            delete cond_tree;
        }
    }
}

int run_fpgrowth(const Database& db, double min_sup) {
    fp_count = 0;
    int min_count = (int)(min_sup * db.size());
    FPTree* tree = new FPTree(min_count);
    tree->build(db);
    fp_mine(tree, {}, min_count);
    delete tree;
    return fp_count;
}


// ---------------------------------------------------------------------------
// Experiment runner
// ---------------------------------------------------------------------------

struct Result {
    string dataset, algorithm;
    double min_sup;
    double avg_time_ms;
    double avg_mem_mb;
    int itemsets;
    int candidates;
    double speedup;
};

vector<Result> all_results;

void run_experiment(const string& dataset_file, double min_sup, int num_runs = 3) {
    cout << "\n" << string(60, '=') << "\n";
    cout << "Dataset: " << dataset_file
         << "  |  min_sup: " << min_sup
         << "  |  runs: " << num_runs << "\n";
    cout << string(60, '=') << "\n";

    Database db = load_dataset(dataset_file);
    if (db.empty()) return;
    cout << "  Transactions loaded: " << db.size() << "\n";

    double base_avg_ms = 0;

    auto run_algo = [&](const string& name, auto fn) -> Result {
        vector<double> times, mems;
        int itemsets = 0, cands = 0;

        for (int r = 0; r < num_runs; r++) {
            double mem_before = get_peak_memory_mb();
            auto start = high_resolution_clock::now();
            auto [its, cs] = fn();
            auto end = high_resolution_clock::now();
            double ms  = duration<double, milli>(end - start).count();
            double mem = get_peak_memory_mb() - mem_before;
            times.push_back(ms);
            mems.push_back(max(0.0, mem));
            itemsets = its;
            cands    = cs;
        }

        double avg_t = accumulate(times.begin(), times.end(), 0.0) / num_runs;
        double avg_m = accumulate(mems.begin(),  mems.end(),  0.0) / num_runs;
        double speedup = (name == "Baseline Apriori") ? 1.0
                       : (base_avg_ms > 0 ? base_avg_ms / avg_t : 0.0);

        cout << fixed << setprecision(2);
        cout << "  " << left  << setw(24) << name
             << " | " << setw(9)  << avg_t   << " ms"
             << " | " << setw(8)  << avg_m   << " MB"
             << " | " << setw(6)  << itemsets << " itemsets"
             << " | speedup: "    << speedup  << "x\n";

        return {dataset_file, name, min_sup, avg_t, avg_m, itemsets, cands, speedup};
    };

    auto r1 = run_algo("Baseline Apriori", [&]() {
        return baseline_apriori(db, min_sup);
    });
    base_avg_ms = r1.avg_time_ms;
    all_results.push_back(r1);

    auto r2 = run_algo("TidList Apriori", [&]() {
        return tidlist_apriori(db, min_sup);
    });
    all_results.push_back(r2);

    auto r3 = run_algo("Parallel Apriori", [&]() {
        return parallel_apriori(db, min_sup);
    });
    all_results.push_back(r3);

    auto r4 = run_algo("FP-Growth", [&]() -> pair<int,int> {
        return {run_fpgrowth(db, min_sup), 0};
    });
    all_results.push_back(r4);
}

void save_csv(const string& filename) {
    ofstream f(filename);
    f << "Dataset,MinSup,Algorithm,AvgTime_ms,AvgRAM_MB,Itemsets,Candidates,Speedup\n";
    for (auto& r : all_results) {
        f << r.dataset    << ","
          << r.min_sup    << ","
          << r.algorithm  << ","
          << fixed << setprecision(4) << r.avg_time_ms << ","
          << r.avg_mem_mb << ","
          << r.itemsets   << ","
          << r.candidates << ","
          << setprecision(2) << r.speedup << "\n";
    }
    cout << "\nResults saved to " << filename << "\n";
}


// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    cout << "FIM Benchmark: Apriori | TidList | Parallel | FP-Growth\n";
    cout << "Hardware threads: " << thread::hardware_concurrency() << "\n";

    // Chess: dense, 3196 transactions, 75 items
    run_experiment("chess.dat", 0.95, 3);
    run_experiment("chess.dat", 0.90, 3);
    run_experiment("chess.dat", 0.85, 3);

    // Connect: very dense, 67557 transactions, 129 items
    run_experiment("connect.dat", 0.99, 3);
    run_experiment("connect.dat", 0.98, 3);
    run_experiment("connect.dat", 0.97, 3);

    // Accidents: large, 340183 transactions, 468 items
    run_experiment("accidents.dat", 0.95, 3);
    run_experiment("accidents.dat", 0.90, 3);
    run_experiment("accidents.dat", 0.85, 3);

    save_csv("cpp_results.csv");

    cout << "\n" << string(100, '=') << "\n";
    cout << left << setw(20) << "Dataset"
                 << setw(8)  << "MinSup"
                 << setw(24) << "Algorithm"
                 << setw(12) << "Time(ms)"
                 << setw(12) << "RAM(MB)"
                 << setw(10) << "Itemsets"
                 << setw(10) << "Speedup" << "\n";
    cout << string(100, '-') << "\n";
    for (auto& r : all_results) {
        cout << fixed << setprecision(2);
        cout << left << setw(20) << r.dataset
                     << setw(8)  << r.min_sup
                     << setw(24) << r.algorithm
                     << setw(12) << r.avg_time_ms
                     << setw(12) << r.avg_mem_mb
                     << setw(10) << r.itemsets
                     << setw(10) << r.speedup << "\n";
    }

    return 0;
}
