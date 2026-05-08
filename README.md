# Frequent Itemset Mining: Algorithmic Comparison and Optimization

**Course:** CS-378 Design and Analysis of Algorithms  
**Authors:** Muhammad Umer, Mohammad Musa Ali, Sarosh Ishaq  
**Institution:** Ghulam Ishaq Khan Institute of Engineering Sciences and Technology (GIKI)

## 📌 Project Overview
This repository contains a fully custom, from-scratch C++17 implementation of four Frequent Itemset Mining (FIM) strategies. The project empirically evaluates the classical Apriori algorithm against the state-of-the-art FP-Growth algorithm across standard FIMI benchmark datasets.

To address Apriori's inherent I/O and CPU bottlenecks, we implemented and analyzed two distinct optimization strategies:
1. **Vertical Data Representation (Tid-List Apriori):** Replacing horizontal database scans with `std::set` intersections.
2. **Parallel Candidate Evaluation (Parallel Apriori):** Utilizing `std::thread` and `std::mutex` for zero-copy, concurrent support counting across hardware cores.

## 📂 Repository Structure
* `/src/` - Contains `run.cpp`, the core engine housing all four algorithms and the custom `FPTree` / `FPNode` data structures.
* `/data/` - The standard FIMI transaction databases used for benchmarking (`chess.dat`, `connect.dat`).
* `/results/` - The raw `cpp_results.csv` data and generated performance visualization graphs.
* `/report/` - The final IEEE double-column research paper detailing our complexity proofs, hardware-level cache analysis, and Amdahl's Law scalability models.

## 🗄️ Dataset Note
Due to GitHub's file size constraints, the `accidents.dat` dataset is not included in this repository. To run the full benchmark suite:
1. Download `accidents.dat` from the official FIMI repository (http://fimi.uantwerpen.be/data/).
2. Place the unzipped `accidents.dat` file inside the `data/` folder before executing the compiled C++ program.

## 🚀 Build and Execution Instructions

### Prerequisites
* A C++17 compatible compiler (e.g., GCC, Clang, or MSVC)
* Datasets must be located in the same working directory as the executable (or update the file paths in the code).

### Compilation (Linux/macOS)
Compile the source code with full optimization (`-O3`) and threading support (`-pthread`):
```bash
g++ -O3 -std=c++17 -pthread -o fim src/run.cpp
```

 ### Run
```bash
./fim
