/*
 Evolve hard Minesweeper puzzles while keeping one fixed underlying solution.

 The program first makes one random mine arrangement and builds one incremental
 CaDiCaL model containing every possible clue for that solution. It then:
   1. makes several independently minimized starting puzzles,
   2. keeps only difficulty=2 puzzles that the one-number/two-number solver
      cannot finish,
   3. restores clues mainly inside a local region,
   4. removes clues again in a new randomized order while preserving uniqueness,
   5. scores the resulting clue-minimal puzzles, and
   6. keeps a pool containing the hardest old and new puzzles.

 Incremental SAT is used only for the many uniqueness checks during clue
 minimization. Difficulty scores based on SAT always use a newly constructed
 fresh solver, so learned clauses from earlier candidates cannot affect scores.

 Build CaDiCaL once with its own ./configure && make, then compile with...
   g++ -std=c++17 -O3 gen_sat_evolve.cpp -Icadical/src cadical/build/libcadical.a -o gen_sat_evolve
 and run with...
   ./gen_sat_evolve
*/



#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "cadical.hpp"




const int numRows = 20;
const int numCols = 20;
const int size = numRows * numCols;

// This first version evolves only difficulty=2 puzzles.
// difficulty=1 means use one-number and two-number logic without guessing.
// difficulty=2 additionally enables recursive guessing.
int difficulty = 2;


// Random solution and evolution parameters

int seed = -1;  // set to -1 to get a random seed

const int numberOfGenerations = 100;
const int poolSize = 30;
const int initialPuzzleTrials = 20;
const int parentsPerGeneration = 10;
const int childrenPerParent = 8;

// A mutation restores hidden clues mostly in one local region, then greedily
// removes clues again in a new randomized order. The fixed 9 by 9 default is
// based on the locality of Minesweeper clues rather than board size. On a board
// smaller than this, the region is capped to the board dimensions. Regions can
// overlap freely from one mutation to the next.
const int mutationRegionRows = 9;
const int mutationRegionCols = 9;

// Fractions are applied to currently hidden safe clue locations.
// A 25% inside density makes a meaningful local disturbance. The tiny outside
// density occasionally allows distant parts of a large puzzle to change too.
const double insideRestoreFraction = 0.25;
const double outsideRestoreFraction = 0.001;
const int minimumInsideCluesToRestore = 3;

// If true, visible clues in the mutation region are tested for removal before
// clues outside it. This gives the restored local clues a chance to replace old
// local clues before the global cleanup pass.
const bool regionCluesFirstInRemovalOrder = true;

// This preserves the same usability rule as gen_sat.cpp: every hidden square
// must touch at least one visible clue. If false, minimization is based only on
// uniqueness.
const bool requireEveryUnknownAdjacentToClue = true;


// Difficulty-scoring parameters

// SAT difficulty always means a fresh SAT solver built from the candidate
// puzzle. The persistent incremental solver is never used for difficulty.
const bool useSatDifficulty = true;

// Select which fresh raw-SAT propagation count is used as the SAT score:
//   0 = first solve: find one solution
//   1 = second solve: find another solution or prove uniqueness
//   2 = total of both solves
// Recent tests found first and total propagations to correlate very strongly
// with the old recursive solver's first-solution depth. Total is the default.
// Ranking by the raw count is identical to ranking by log10(count+1).
const int satDifficultyPropagationMode = 2;

// Recursive scoring can be enabled as an expensive authority/validation layer.
// If false, the pool is ranked entirely by fresh SAT propagations.
// If true, only the top fraction of new children by SAT score are recursively
// scored, and only those children can enter the pool.
const bool useRecursiveDifficulty = false;
const double fractionToRecursiveScore = 0.10;

// Number of best final puzzles printed in full.
const int finalPuzzlesToPrint = 5;


// make a lookup table that is nCr(n,r)
const int nCr[9][9] = {
    {1,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0},
    {1,3,3,1,0,0,0,0,0},
    {1,4,6,4,1,0,0,0,0},
    {1,5,10,10,5,1,0,0,0},
    {1,6,15,20,15,6,1,0,0},
    {1,7,21,35,35,21,7,1,0},
    {1,8,28,56,70,56,28,8,1}
};


// These "sets" are tiny, so vector<int> plus linear search is usually faster
//   and simpler than std::set or std::unordered_set.
using SmallSet = std::vector<int>;


int ind(int row, int col) {
  return row * numCols + col;
}


int rowOf(int index) {
  return index / numCols;
}


int colOf(int index) {
  return index % numCols;
}


bool contains(const SmallSet& v, int value) {
  return std::find(v.begin(), v.end(), value) != v.end();
}


void addToSet(SmallSet& v, int value) {
  if (!contains(v, value)) {
    v.push_back(value);
  }
}


void removeFromSet(SmallSet& v, int value) {
  auto it = std::find(v.begin(), v.end(), value);
  if (it != v.end()) {
    *it = v.back();
    v.pop_back();
  }
}


SmallSet intersection(const SmallSet& a, const SmallSet& b) {
  SmallSet out;
  for (int x : a) {
    if (contains(b, x)) {
      out.push_back(x);
    }
  }
  return out;
}


SmallSet difference(const SmallSet& a, const SmallSet& b) {
  SmallSet out;
  for (int x : a) {
    if (!contains(b, x)) {
      out.push_back(x);
    }
  }
  return out;
}


void printPuzzle(const std::vector<int>& ar) {
  for (int i = 0; i < numRows; i++) {
    for (int j = 0; j < numCols; j++) {
      std::cout << ar[ind(i,j)];
      if (j + 1 < numCols) {
        std::cout << " ";
      }
    }
    std::cout << "\n";
  }
}


std::string arrayToString(const std::vector<int>& ar) {
  std::string s;
  s.reserve(ar.size());
  for (int x : ar) {
    s += char('0' + x);
  }
  return s;
}


uint32_t rotr(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32 - n));
}


// Minimal SHA-256 implementation so the generator stays dependency-free.
std::string sha256(const std::string& input) {
  static const uint32_t k[64] = {
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
  };

  uint64_t bitLen = uint64_t(input.size()) * 8;
  std::vector<uint8_t> msg(input.begin(), input.end());
  msg.push_back(0x80);
  while ((msg.size() % 64) != 56) {
    msg.push_back(0);
  }
  for (int i = 7; i >= 0; i--) {
    msg.push_back(uint8_t((bitLen >> (8 * i)) & 0xff));
  }

  uint32_t h0 = 0x6a09e667;
  uint32_t h1 = 0xbb67ae85;
  uint32_t h2 = 0x3c6ef372;
  uint32_t h3 = 0xa54ff53a;
  uint32_t h4 = 0x510e527f;
  uint32_t h5 = 0x9b05688c;
  uint32_t h6 = 0x1f83d9ab;
  uint32_t h7 = 0x5be0cd19;

  for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
      size_t p = chunk + 4 * i;
      w[i] = (uint32_t(msg[p]) << 24) | (uint32_t(msg[p+1]) << 16) |
             (uint32_t(msg[p+2]) << 8) | uint32_t(msg[p+3]);
    }
    for (int i = 16; i < 64; i++) {
      uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
      uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
      w[i] = w[i-16] + s0 + w[i-7] + s1;
    }

    uint32_t a = h0;
    uint32_t b = h1;
    uint32_t c = h2;
    uint32_t d = h3;
    uint32_t e = h4;
    uint32_t f = h5;
    uint32_t g = h6;
    uint32_t h = h7;

    for (int i = 0; i < 64; i++) {
      uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      uint32_t ch = (e & f) ^ ((~e) & g);
      uint32_t temp1 = h + S1 + ch + k[i] + w[i];
      uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint32_t temp2 = S0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
    h5 += f;
    h6 += g;
    h7 += h;
  }

  std::ostringstream out;
  out << std::hex << std::setfill('0');
  out << std::setw(8) << h0 << std::setw(8) << h1 << std::setw(8) << h2 << std::setw(8) << h3;
  out << std::setw(8) << h4 << std::setw(8) << h5 << std::setw(8) << h6 << std::setw(8) << h7;
  return out.str();
}


//////// make a solve function that does not do guessing
// It will look at a single number at a time, or two numbers at a time.


// Returns -1 if there is proven to be 0 solutions.
// Modifies remaining, flags, unknowns, and connectionsPairs.
int solve(std::vector<int>& remaining,
          std::vector<int>& flags,
          std::vector<SmallSet>& unknowns,
          std::vector<SmallSet>& connectionsPairs,
          const std::vector<SmallSet>& connections) {

  bool change = true;

  while (change) {

    change = false;

    for (int i = int(remaining.size()) - 1; i >= 0; i--) {
      int current = remaining[i];

      // check for solved numbers (look at a single number at a time)
      if (!flags[current]) {
        change = true;
        // update remaining and unknowns
        remaining.erase(remaining.begin() + i);
        SmallSet currentUnknowns = unknowns[current];
        for (int j : currentUnknowns) {
          for (int c : connections[j]) {
            removeFromSet(unknowns[c], j);
            if (unknowns[c].size() < size_t(flags[c])) {
              return -1;
            }
          }
        }
      }

      // check for solved numbers (look at a single number at a time)
      else if (flags[current] == int(unknowns[current].size())) {
        change = true;
        // update remaining, flags, sol, and unknowns
        remaining.erase(remaining.begin() + i);
        SmallSet currentUnknowns = unknowns[current];
        for (int j : currentUnknowns) {
          for (int c : connections[j]) {
            if (flags[c]) {
              flags[c] -= 1;
              removeFromSet(unknowns[c], j);
            } else {
              return -1;
            }
          }
        }
      }

      // look at pairs at a time
      else if (difficulty && unknowns[current].size() > 1) {  // at least 2 in intersection to be useful
        SmallSet pairsToCheck = connectionsPairs[current];
        for (int k : pairsToCheck) {

          SmallSet intersect = intersection(unknowns[current], unknowns[k]);
          int L3 = int(intersect.size());
          if (L3 < 2) {
            removeFromSet(connectionsPairs[current], k);
            continue;
          }
          SmallSet set1 = difference(unknowns[current], intersect);
          SmallSet set2 = difference(unknowns[k], intersect);
          int L1 = int(set1.size());
          int L2 = int(set2.size());
          if (!(L1 || L2)) {
            removeFromSet(connectionsPairs[current], k);
            continue;
          }
          int maxFlagsInIntersection = std::min({flags[current], flags[k], L3});

          bool changeTemp = false;
          if (flags[current] == maxFlagsInIntersection && flags[k] == L2 + maxFlagsInIntersection) {
            if (L2) {   // add flags to the unknowns in set2
              changeTemp = true;
              for (int j : set2) {   // loop over each flag that is placed
                for (int c : connections[j]) {
                  if (flags[c]) {
                    flags[c] -= 1;
                    removeFromSet(unknowns[c], j);
                  } else {
                    return -1;
                  }
                }
              }
            }
            if (L1) {   // clear the unknowns in set1
              changeTemp = true;
              for (int j : set1) {
                for (int c : connections[j]) {
                  removeFromSet(unknowns[c], j);
                  if (unknowns[c].size() < size_t(flags[c])) {
                    return -1;
                  }
                }
              }
            }
          } else if (flags[k] == maxFlagsInIntersection && flags[current] == L1 + maxFlagsInIntersection) {
            if (L1) {   // add flags to the unknowns in set1
              changeTemp = true;
              for (int j : set1) {   // loop over each flag that is placed
                for (int c : connections[j]) {
                  if (flags[c]) {
                    flags[c] -= 1;
                    removeFromSet(unknowns[c], j);
                  } else {
                    return -1;
                  }
                }
              }
            }
            if (L2) {   // clear the unknowns in set2
              changeTemp = true;
              for (int j : set2) {
                for (int c : connections[j]) {
                  removeFromSet(unknowns[c], j);
                  if (unknowns[c].size() < size_t(flags[c])) {
                    return -1;
                  }
                }
              }
            }
          }

          if (changeTemp) {
            removeFromSet(connectionsPairs[current], k);
            change = true;
          }
        }
      }
    }
  }

  return 0;
}





//////// make a solve function that does a breadth-first search at a depth of 1
// For each in remaining, try a flag or a clear. If there is a contradiction, the other must be correct.
//
// I do not use this. It slows down the solver.
// Also, this function is experimental and not maintained.
// If I ever did use it, I would be sure to add the depth parameters so that
//   they still print when difficulty=2, where depth would be the depth of the
//   RECURSIVE solver.


// Returns -1 if there is proven to be 0 solutions.
// Modifies remaining, flags, unknowns, and connectionsPairs.
int solCount = 0;
int breadth_first_search(std::vector<int>& remaining,
                         std::vector<int>& flags,
                         std::vector<SmallSet>& unknowns,
                         std::vector<SmallSet>& connectionsPairs,
                         const std::vector<SmallSet>& connections) {

  bool change = true;

  while (change) {   // keep going as long as progress is being made!

    change = false;

    // instead I should create and update a list of unknowns instead??
    for (int i = 0; i < numRows; i++) {
     for (int j = 0; j < numCols; j++) {

      int current = ind(i,j);

      // make sure that this is an unknown
      if (connections[current].empty()) {
        continue;
      }
      if (!contains(unknowns[connections[current][0]], current)) {
        continue;
      }


      // try adding a flag

      // shallow copy as much as possible (greatly speeds things up compared to deep copy!)
      std::vector<SmallSet> u2 = unknowns;
      std::vector<SmallSet> c2 = connectionsPairs;
      std::vector<int> r2 = remaining;
      std::vector<int> f2 = flags;

      // add flag
      bool contradiction = false;
      for (int c : connections[current]) {
        if (f2[c]) {
          f2[c] -= 1;
          removeFromSet(u2[c], current);
        } else {
          contradiction = true;
          break;
        }
      }

      // call solve()
      if (!contradiction) {
        if (solve(r2, f2, u2, c2, connections) == -1) {
          contradiction = true;
        }
      }

      if (contradiction) {  // this square is a clear!

        // clear the location
        for (int c : connections[current]) {
          removeFromSet(unknowns[c], current);
          if (unknowns[c].size() < size_t(flags[c])) {
            return -1;
          }
        }

        if (solve(remaining, flags, unknowns, connectionsPairs, connections) == -1) {
          return -1;
        }

        if (remaining.size() == 0) {
          return 0;
        }

        change = true;
        //std::cout << ",\n";
        continue;
      }


      // try adding a clear

      // shallow copy as much as possible (greatly speeds things up compared to deep copy!)
      std::vector<SmallSet> u3 = unknowns;
      std::vector<SmallSet> c3 = connectionsPairs;
      std::vector<int> r3 = remaining;
      std::vector<int> f3 = flags;

      // update u3
      contradiction = false;
      for (int c : connections[current]) {
        removeFromSet(u3[c], current);
        if (u3[c].size() < size_t(f3[c])) {
          contradiction = true;
          break;
        }
      }

      // call solve()
      if (!contradiction) {
        if (solve(r3, f3, u3, c3, connections) == -1) {
          contradiction = true;
        }
      }

      if (contradiction) {  // this square is a flag!

        unknowns = u2;
        connectionsPairs = c2;
        remaining = r2;
        flags = f2;

        if (remaining.size() == 0) {
          return 0;
        }

        change = true;
        //std::cout << ".\n";
        continue;
      }


      // handle if either or both situations were solved

      if (r3.size() == 0 && r2.size() == 0) {  // handle both being solved
        solCount += 2;
        return 0;
      }

      else if (r3.size() == 0) {          // r3 is solved
        solCount += 1;
        if (solCount > 1) {
          return 0;
        }

        unknowns = u2;
        connectionsPairs = c2;
        remaining = r2;
        flags = f2;

        change = true;
      }

      else if (r2.size() == 0) {          // r2 is solved
        solCount += 1;
        if (solCount > 1) {
          return 0;
        }

        unknowns = u3;
        connectionsPairs = c3;
        remaining = r3;
        flags = f3;

        change = true;
      }
     }
    }
  }

  return 0;
}



//////// the recursive solver function



// solCount should be 0 before calling this function, then...
//  0 if the puzzle was not solved (if difficulty=2, this means the puzzle actually has 0 solutions, so this never occurs for difficulty=2)
//  1 if there is 1 unique solution
//  2+ if there are 2+ or more solutions (only occurs if difficulty=2)

int finalBranches = 0;  // value does not matter yet, but we must define the global variable
// This variable is useful for analyzing a difficulty=2 puzzle that is known to have 1 unique solution.

// Extra recursive-solver metrics for comparing against fresh SAT metrics.
long long oldGuessBranches = 0;
long long oldRecursiveCalls = 0;
int oldMaximumDepthVisited = 0;
int oldFirstSolutionDepth = -1;
int oldMinimumSolutionDepth = -1;
int oldMaximumSolutionDepth = -1;


template <class Func>
void forEachCombination(const SmallSet& items, int r, Func func) {
  int n = int(items.size());
  if (r < 0 || r > n) {
    return;
  }
  if (r == 0) {
    SmallSet combo;
    func(combo);
    return;
  }

  std::vector<int> indexes(r);
  for (int i = 0; i < r; i++) {
    indexes[i] = i;
  }

  while (true) {
    SmallSet combo;
    combo.reserve(r);
    for (int x : indexes) {
      combo.push_back(items[x]);
    }
    if (!func(combo)) {
      return;
    }

    int i = r - 1;
    while (i >= 0 && indexes[i] == i + n - r) {
      i--;
    }
    if (i < 0) {
      return;
    }
    indexes[i]++;
    for (int j = i + 1; j < r; j++) {
      indexes[j] = indexes[j-1] + 1;
    }
  }
}



//////// incremental SAT solver used for generation and minimization

// There is one mine variable for every square and one selector variable for
// every safe square in the fixed full solution. A candidate puzzle is supplied
// only through selector assumptions. Permanent clauses and useful learned
// information are retained across all nearby candidate puzzles.
CaDiCaL::Solver incrementalSatSolver;
std::vector<int> incrementalSatMineVars(size, 0);
std::vector<int> incrementalSatClueSelectors(size, 0);
int nextIncrementalSatVar = 1;
long long incrementalSatChecks = 0;


void addIncrementalSatClause(const std::vector<int>& clause) {
  incrementalSatSolver.clause(clause);
}


void addIncrementalGuardedExactly(int selector,
                                  const SmallSet& vars,
                                  int wanted) {
  int n = int(vars.size());

  if (wanted < 0 || wanted > n) {
    incrementalSatSolver.clause(-selector);
    return;
  }

  // At most wanted: every group of wanted+1 variables cannot all be true.
  forEachCombination(vars, wanted + 1, [&](const SmallSet& combo) {
    std::vector<int> clause;
    clause.reserve(combo.size() + 1);
    clause.push_back(-selector);
    for (int x : combo) {
      clause.push_back(-x);
    }
    addIncrementalSatClause(clause);
    return true;
  });

  // At least wanted: every group of n-wanted+1 variables cannot all be false.
  forEachCombination(vars, n - wanted + 1, [&](const SmallSet& combo) {
    std::vector<int> clause;
    clause.reserve(combo.size() + 1);
    clause.push_back(-selector);
    for (int x : combo) {
      clause.push_back(x);
    }
    addIncrementalSatClause(clause);
    return true;
  });
}


void initializeIncrementalSatSolver(const std::vector<int>& fullPuzzle) {
  int clueCount = 0;
  for (int x : fullPuzzle) {
    if (x != -1) {
      clueCount++;
    }
  }

  // Explicit declaration prevents CaDiCaL preprocessing from taking variable
  // indexes that this code later intends to use.
  incrementalSatSolver.declare_more_variables(size + clueCount);

  for (int i = 0; i < size; i++) {
    incrementalSatMineVars[i] = nextIncrementalSatVar++;
  }

  for (int i = 0; i < size; i++) {
    if (fullPuzzle[i] == -1) {
      continue;   // actual mines can never be visible clues
    }

    int selector = nextIncrementalSatVar++;
    incrementalSatClueSelectors[i] = selector;

    // Activating a clue also requires its own square to be clear.
    incrementalSatSolver.clause(-selector, -incrementalSatMineVars[i]);

    SmallSet neighborVars;
    neighborVars.reserve(8);
    int row = rowOf(i);
    int col = colOf(i);
    for (int row2 = std::max(0,row-1); row2 < std::min(numRows,row+2); row2++) {
      for (int col2 = std::max(0,col-1); col2 < std::min(numCols,col+2); col2++) {
        if (row2 == row && col2 == col) {
          continue;
        }
        neighborVars.push_back(incrementalSatMineVars[ind(row2,col2)]);
      }
    }

    addIncrementalGuardedExactly(selector, neighborVars, fullPuzzle[i]);
  }
}


void assumeIncrementalSatPuzzle(const std::vector<int>& puzzle) {
  for (int i = 0; i < size; i++) {
    if (!incrementalSatClueSelectors[i]) {
      continue;
    }

    if (puzzle[i] == -1) {
      incrementalSatSolver.assume(-incrementalSatClueSelectors[i]);
    } else {
      incrementalSatSolver.assume(incrementalSatClueSelectors[i]);
    }
  }
}


int countSolutionsIncrementalSat(const std::vector<int>& puzzle) {
  incrementalSatChecks++;

  assumeIncrementalSatPuzzle(puzzle);
  int result = incrementalSatSolver.solve();

  if (result == CaDiCaL::UNSATISFIABLE) {
    return 0;
  }
  if (result != CaDiCaL::SATISFIABLE) {
    std::cerr << "Incremental SAT solver returned UNKNOWN!\n";
    std::exit(1);
  }

  // Read the entire model before constrain(), which invalidates val().
  std::vector<bool> firstSolution(size);
  for (int i = 0; i < size; i++) {
    firstSolution[i] =
        incrementalSatSolver.val(incrementalSatMineVars[i]) > 0;
  }

  // This temporary blocking clause lasts only for the next solve.
  for (int i = 0; i < size; i++) {
    incrementalSatSolver.constrain(
        firstSolution[i]
            ? -incrementalSatMineVars[i]
            : incrementalSatMineVars[i]
    );
  }
  incrementalSatSolver.constrain(0);

  // Assumptions also last for only one solve.
  assumeIncrementalSatPuzzle(puzzle);
  result = incrementalSatSolver.solve();

  if (result == CaDiCaL::SATISFIABLE) {
    return 2;
  }
  if (result == CaDiCaL::UNSATISFIABLE) {
    return 1;
  }

  std::cerr << "Incremental SAT solver returned UNKNOWN!\n";
  std::exit(1);
}


//////// fresh SAT solvers used only for difficulty metrics

// Raw SAT builds the current puzzle from scratch. After-logic SAT builds the
// smaller residual problem left by solve(), also from scratch. Fresh solvers
// make statistics comparable between candidate puzzles; incremental learned
// clauses would make the score depend on which puzzles happened to be tested
// earlier.

struct SatStatistics {
  int64_t conflicts = -1;
  int64_t decisions = -1;
  int64_t propagations = -1;
};


struct FreshSatResult {
  int solCount = -1;
  int unknownCount = 0;
  int clueCount = 0;
  int variableCount = 0;
  int64_t clauseCount = 0;
  bool ranSecondSolve = false;

  double buildTime = 0;
  double firstTime = 0;
  double secondTime = 0;

  SatStatistics first;
  SatStatistics second;
  SatStatistics total;
};


int64_t subtractStatistic(int64_t after, int64_t before) {
  if (after < 0 || before < 0) {
    return -1;
  }
  return after - before;
}


SatStatistics subtractStatistics(const SatStatistics& after,
                                 const SatStatistics& before) {
  SatStatistics answer;
  answer.conflicts = subtractStatistic(after.conflicts, before.conflicts);
  answer.decisions = subtractStatistic(after.decisions, before.decisions);
  answer.propagations = subtractStatistic(after.propagations, before.propagations);
  return answer;
}


SatStatistics getSatStatistics(const CaDiCaL::Solver& solver) {
  SatStatistics answer;
  answer.conflicts = solver.get_statistic_value("conflicts");
  answer.decisions = solver.get_statistic_value("decisions");
  answer.propagations = solver.get_statistic_value("propagations");
  return answer;
}


void checkSatStatisticNames() {
  CaDiCaL::Solver solver;
  const char* names[] = {"conflicts", "decisions", "propagations"};
  for (const char* name : names) {
    if (solver.get_statistic_value(name) < 0) {
      std::cerr << "Warning: CaDiCaL statistic '" << name
                << "' is unavailable; its output columns will contain -1.\n";
    }
  }
}


double normalizedStatistic(int64_t value, int denominator) {
  if (value < 0) {
    return -1;
  }
  return double(value) / std::max(1, denominator);
}


double normalizedTime(double value, int denominator) {
  return value / std::max(1, denominator);
}


void addSatClause(CaDiCaL::Solver& solver, const std::vector<int>& clause) {
  solver.clause(clause);
}


void addExactly(CaDiCaL::Solver& solver,
                const SmallSet& vars,
                int wanted,
                int64_t& clauseCount) {
  int n = int(vars.size());

  if (wanted < 0 || wanted > n) {
    solver.add(0);   // empty clause: this residual problem is impossible
    clauseCount++;
    return;
  }

  // At most wanted: every group of wanted+1 variables cannot all be true.
  forEachCombination(vars, wanted + 1, [&](const SmallSet& combo) {
    std::vector<int> clause;
    clause.reserve(combo.size());
    for (int x : combo) {
      clause.push_back(-x);
    }
    addSatClause(solver, clause);
    clauseCount++;
    return true;
  });

  // At least wanted: every group of n-wanted+1 variables cannot all be false.
  forEachCombination(vars, n - wanted + 1, [&](const SmallSet& combo) {
    std::vector<int> clause;
    clause.reserve(combo.size());
    for (int x : combo) {
      clause.push_back(x);
    }
    addSatClause(solver, clause);
    clauseCount++;
    return true;
  });
}


FreshSatResult finishFreshSatSolve(CaDiCaL::Solver& solver,
                                   const std::vector<int>& satVars,
                                   FreshSatResult answer,
                                   const SatStatistics& beforeSolve) {

  auto firstStart = std::chrono::steady_clock::now();
  int result = solver.solve();
  auto firstEnd = std::chrono::steady_clock::now();
  answer.firstTime = std::chrono::duration<double>(firstEnd - firstStart).count();

  SatStatistics afterFirst = getSatStatistics(solver);
  answer.first = subtractStatistics(afterFirst, beforeSolve);

  if (result == CaDiCaL::UNSATISFIABLE) {
    answer.solCount = 0;
    answer.total = answer.first;
    return answer;
  }
  if (result != CaDiCaL::SATISFIABLE) {
    std::cerr << "Fresh SAT solver returned UNKNOWN!\n";
    std::exit(1);
  }

  if (satVars.empty()) {
    answer.solCount = 1;
    answer.total = answer.first;
    return answer;
  }

  // Read the complete first solution before calling constrain().
  std::vector<bool> firstSolution(satVars.size());
  for (size_t i = 0; i < satVars.size(); i++) {
    firstSolution[i] = solver.val(satVars[i]) > 0;
  }

  // This clause lasts only for the next solve and requires a different model.
  for (size_t i = 0; i < satVars.size(); i++) {
    solver.constrain(firstSolution[i] ? -satVars[i] : satVars[i]);
  }
  solver.constrain(0);

  answer.ranSecondSolve = true;
  auto secondStart = std::chrono::steady_clock::now();
  result = solver.solve();
  auto secondEnd = std::chrono::steady_clock::now();
  answer.secondTime = std::chrono::duration<double>(secondEnd - secondStart).count();

  SatStatistics afterSecond = getSatStatistics(solver);
  answer.second = subtractStatistics(afterSecond, afterFirst);
  answer.total = subtractStatistics(afterSecond, beforeSolve);

  if (result == CaDiCaL::SATISFIABLE) {
    answer.solCount = 2;
  } else if (result == CaDiCaL::UNSATISFIABLE) {
    answer.solCount = 1;
  } else {
    std::cerr << "Fresh SAT solver returned UNKNOWN!\n";
    std::exit(1);
  }

  return answer;
}


FreshSatResult solveFreshSatRaw(const std::vector<int>& puzzle) {
  FreshSatResult answer;
  auto buildStart = std::chrono::steady_clock::now();

  CaDiCaL::Solver solver;
  std::vector<int> cellToVar(size, 0);
  std::vector<int> satVars;
  satVars.reserve(size);

  for (int i = 0; i < size; i++) {
    if (puzzle[i] == -1) {
      answer.unknownCount++;
    } else {
      answer.clueCount++;
    }
  }

  if (answer.unknownCount) {
    solver.declare_more_variables(answer.unknownCount);
  }

  int nextVar = 1;
  for (int i = 0; i < size; i++) {
    if (puzzle[i] == -1) {
      cellToVar[i] = nextVar++;
      satVars.push_back(cellToVar[i]);
    }
  }
  answer.variableCount = int(satVars.size());

  for (int i = 0; i < size; i++) {
    if (puzzle[i] == -1) {
      continue;
    }

    SmallSet neighborVars;
    neighborVars.reserve(8);
    int row = rowOf(i);
    int col = colOf(i);
    for (int row2 = std::max(0,row-1); row2 < std::min(numRows,row+2); row2++) {
      for (int col2 = std::max(0,col-1); col2 < std::min(numCols,col+2); col2++) {
        int here = ind(row2,col2);
        if (puzzle[here] == -1) {
          neighborVars.push_back(cellToVar[here]);
        }
      }
    }

    addExactly(solver, neighborVars, puzzle[i], answer.clauseCount);
  }

  auto buildEnd = std::chrono::steady_clock::now();
  answer.buildTime = std::chrono::duration<double>(buildEnd - buildStart).count();
  SatStatistics beforeSolve = getSatStatistics(solver);
  return finishFreshSatSolve(solver, satVars, answer, beforeSolve);
}


FreshSatResult solveFreshSatAfterLogic(const std::vector<int>& remaining,
                                        const std::vector<int>& flags,
                                        const std::vector<SmallSet>& unknowns) {
  FreshSatResult answer;
  auto buildStart = std::chrono::steady_clock::now();

  CaDiCaL::Solver solver;
  std::vector<char> isResidualUnknown(size, 0);
  for (int current : remaining) {
    for (int cell : unknowns[current]) {
      isResidualUnknown[cell] = 1;
    }
  }

  for (int i = 0; i < size; i++) {
    if (isResidualUnknown[i]) {
      answer.unknownCount++;
    }
  }
  answer.clueCount = int(remaining.size());

  if (answer.unknownCount) {
    solver.declare_more_variables(answer.unknownCount);
  }

  std::vector<int> cellToVar(size, 0);
  std::vector<int> satVars;
  satVars.reserve(answer.unknownCount);
  int nextVar = 1;
  for (int i = 0; i < size; i++) {
    if (isResidualUnknown[i]) {
      cellToVar[i] = nextVar++;
      satVars.push_back(cellToVar[i]);
    }
  }
  answer.variableCount = int(satVars.size());

  for (int current : remaining) {
    SmallSet vars;
    vars.reserve(unknowns[current].size());
    for (int cell : unknowns[current]) {
      vars.push_back(cellToVar[cell]);
    }
    addExactly(solver, vars, flags[current], answer.clauseCount);
  }

  auto buildEnd = std::chrono::steady_clock::now();
  answer.buildTime = std::chrono::duration<double>(buildEnd - buildStart).count();
  SatStatistics beforeSolve = getSatStatistics(solver);
  return finishFreshSatSolve(solver, satVars, answer, beforeSolve);
}


void recursive_func(std::vector<int>& remaining,
                    std::vector<int>& flags,
                    std::vector<SmallSet>& unknowns,
                    std::vector<SmallSet>& connectionsPairs,
                    const std::vector<SmallSet>& connections,
                    bool printDepth=false,
                    int depth=0) {

  oldRecursiveCalls++;
  oldMaximumDepthVisited = std::max(oldMaximumDepthVisited, depth);

  // solve whatever we can solve without guessing. This greatly speeds things up!
  if (solve(remaining, flags, unknowns, connectionsPairs, connections) == -1) {
    finalBranches += 1;
    return;
  }

  if (!remaining.size()) {   // if solved
    if (printDepth) {
      std::cout << "depth = " << depth << "\n";
    }
    if (oldFirstSolutionDepth < 0) {
      oldFirstSolutionDepth = depth;
    }
    if (oldMinimumSolutionDepth < 0 || depth < oldMinimumSolutionDepth) {
      oldMinimumSolutionDepth = depth;
    }
    oldMaximumSolutionDepth = std::max(oldMaximumSolutionDepth, depth);
    solCount += 1;
    finalBranches += 1;
    return;
  }

  if (difficulty < 2) {
    return;
  }


  // this does not speed things up, though could possibly be used to define another difficulty level
  /*
  if (!depth) {
    if (breadth_first_search(remaining, flags, unknowns, connectionsPairs, connections) == -1) {
      finalBranches += 1;
      return;
    }

    if (!remaining.size()) {   // if solved
      if (printDepth) {
        std::cout << "depth = " << depth << "\n";
      }
      solCount += 1;
      finalBranches += 1;
      return;
    }
  }
  */


  std::vector<int> r3 = remaining;
  std::sort(r3.begin(), r3.end(), [&](int a, int b) {
    return nCr[unknowns[a].size()][flags[a]] > nCr[unknowns[b].size()][flags[b]];
  });   // this sorting that has us use fewest combinations first GREATLY improves the speed!
  int current = r3.back();
  r3.pop_back();
/*
  // instead, of above code, do the same but smarter without actually sorting (just search for best)

  int bestInd = 0;
  int bestComb = nCr[unknowns[remaining[0]].size()][flags[remaining[0]]];

  for (int i = 1; i < int(remaining.size()); i++) {
    int c = remaining[i];
    int comb = nCr[unknowns[c].size()][flags[c]];
    if (comb <= bestComb) {
      bestComb = comb;
      bestInd = i;
    }
  }

  int current = remaining[bestInd];

  std::vector<int> r3 = remaining;
  r3.erase(r3.begin() + bestInd);
*/



  if (!nCr[unknowns[current].size()][flags[current]]) {
    std::cout << "this happens!\n";
    finalBranches += 1;
    return;   // unnecessary
  }

  // forEachCombination(items, 0, ...) calls the function once with an empty combo.
  // forEachCombination(items, r, ...) does nothing when r > items.size().
  forEachCombination(unknowns[current], flags[current], [&](const SmallSet& combo) {

    if (solCount > 1) {    // stop counting after 2
      return false;
    }

    oldGuessBranches++;

    // shallow copy (greatly speeds things up compared to deep copy!)
    std::vector<SmallSet> u2 = unknowns;

    // update u2
    for (int j : unknowns[current]) {
      for (int c : connections[j]) {
        removeFromSet(u2[c], j);
      }
    }

    std::vector<SmallSet> c2 = connectionsPairs;

    std::vector<int> r2 = r3;
    std::vector<int> f2 = flags;

    // The following is faster than making it a function that can use return to cut out of it
    bool stop = false;
    for (int j : combo) {   // loop over each flag that is placed
      for (int c : connections[j]) {
          if (f2[c]) {
            f2[c] -= 1;
          } else {
            stop = true;
            break;
          }
      }
    }
    if (stop) {
      finalBranches += 1;
      return true;
    }

    // Check if there are fewer unknowns than flags to place.
    // This must be done after flags[] is updated.
    stop = false;
    for (int j : unknowns[current]) {
      //if stop:
      //  break
      for (int c : connections[j]) {
        if (u2[c].size() < size_t(f2[c])) {  // unnecessarily checks for "flag unknowns" too
           stop = true;
           break;
        }
      }
    }
    if (stop) {
      finalBranches += 1;
      return true;
    }

    recursive_func(r2, f2, u2, c2, connections, printDepth, depth+1);
    return true;
  });
}


//////// difficulty-metric exploration wrapper

struct OldSolverResult {
  int solCount = -1;
  double time = 0;
  long long guessBranches = 0;
  long long recursiveCalls = 0;
  int finalBranches = 0;
  int maximumDepthVisited = 0;
  int firstSolutionDepth = -1;
  int minimumSolutionDepth = -1;
  int maximumSolutionDepth = -1;
};


OldSolverResult runOldSolver(const std::vector<int>& remainingIn,
                             const std::vector<int>& flagsIn,
                             const std::vector<SmallSet>& unknownsIn,
                             const std::vector<SmallSet>& connectionsPairsIn,
                             const std::vector<SmallSet>& connections) {
  std::vector<int> remaining = remainingIn;
  std::vector<int> flags = flagsIn;
  std::vector<SmallSet> unknowns = unknownsIn;
  std::vector<SmallSet> connectionsPairs = connectionsPairsIn;

  solCount = 0;
  finalBranches = 0;
  oldGuessBranches = 0;
  oldRecursiveCalls = 0;
  oldMaximumDepthVisited = 0;
  oldFirstSolutionDepth = -1;
  oldMinimumSolutionDepth = -1;
  oldMaximumSolutionDepth = -1;

  auto start = std::chrono::steady_clock::now();
  recursive_func(remaining, flags, unknowns, connectionsPairs, connections);
  auto end = std::chrono::steady_clock::now();

  OldSolverResult answer;
  answer.solCount = solCount;
  answer.time = std::chrono::duration<double>(end - start).count();
  answer.guessBranches = oldGuessBranches;
  answer.recursiveCalls = oldRecursiveCalls;
  answer.finalBranches = finalBranches;
  answer.maximumDepthVisited = oldMaximumDepthVisited;
  answer.firstSolutionDepth = oldFirstSolutionDepth;
  answer.minimumSolutionDepth = oldMinimumSolutionDepth;
  answer.maximumSolutionDepth = oldMaximumSolutionDepth;
  return answer;
}



//////// helper functions for evolving clue subsets


struct SolverData {
  std::vector<int> remaining;
  std::vector<int> flags;
  std::vector<SmallSet> unknowns;
  std::vector<SmallSet> connectionsPairs;
  std::vector<SmallSet> connections;
};


void buildSolverData(const std::vector<int>& puzzle, SolverData& data) {
  data.remaining.clear();
  data.remaining.reserve(size);
  data.flags.assign(size, 0);
  data.unknowns.assign(size, SmallSet());
  data.connectionsPairs.assign(size, SmallSet());
  data.connections.assign(size, SmallSet());

  for (int row = 0; row < numRows; row++) {
    for (int col = 0; col < numCols; col++) {
      int here = ind(row,col);

      if (puzzle[here] == -1) {
        SmallSet clueConnections;
        clueConnections.reserve(8);
        for (int row2 = std::max(0,row-1);
             row2 < std::min(numRows,row+2); row2++) {
          for (int col2 = std::max(0,col-1);
               col2 < std::min(numCols,col+2); col2++) {
            if (row2 == row && col2 == col) {
              continue;
            }
            int there = ind(row2,col2);
            if (puzzle[there] != -1) {
              clueConnections.push_back(there);
            }
          }
        }
        data.connections[here] = clueConnections;
        continue;
      }

      data.remaining.push_back(here);
      data.flags[here] = puzzle[here];

      SmallSet clueUnknowns;
      clueUnknowns.reserve(8);
      for (int row2 = std::max(0,row-1);
           row2 < std::min(numRows,row+2); row2++) {
        for (int col2 = std::max(0,col-1);
             col2 < std::min(numCols,col+2); col2++) {
          int there = ind(row2,col2);
          if (puzzle[there] == -1) {
            clueUnknowns.push_back(there);
          }
        }
      }
      data.unknowns[here] = clueUnknowns;

      // List every useful nearby clue pair once. These are clues in a 5 by 5
      // box, except for its four corners.
      SmallSet pairs;
      pairs.reserve(20);
      for (int row2 = row; row2 < std::min(numRows,row+3); row2++) {
        for (int col2 = std::max(0,col-2);
             col2 < std::min(numCols,col+3); col2++) {
          int there = ind(row2,col2);
          if (puzzle[there] == -1) {
            continue;
          }
          if ((row2-row) == 2 && std::abs(col-col2) == 2) {
            continue;
          }
          if (row2 > row || (row2 == row && col2 > col)) {
            pairs.push_back(there);
          }
        }
      }
      data.connectionsPairs[here] = pairs;
    }
  }
}


// Returns:
//   -1 = contradiction
//    0 = one-number/two-number logic became stuck
//    1 = solved without recursive guessing
int runNonRecursiveSolver(const std::vector<int>& puzzle) {
  SolverData data;
  buildSolverData(puzzle, data);

  int oldDifficulty = difficulty;
  difficulty = 1;
  int result = solve(data.remaining, data.flags, data.unknowns,
                     data.connectionsPairs, data.connections);
  difficulty = oldDifficulty;

  if (result == -1) {
    return -1;
  }
  if (data.remaining.empty()) {
    return 1;
  }
  return 0;
}


OldSolverResult runRecursiveDifficulty(const std::vector<int>& puzzle) {
  SolverData data;
  buildSolverData(puzzle, data);

  int oldDifficulty = difficulty;
  difficulty = 2;
  OldSolverResult answer =
      runOldSolver(data.remaining, data.flags, data.unknowns,
                   data.connectionsPairs, data.connections);
  difficulty = oldDifficulty;
  return answer;
}


bool hasVisibleClueNeighbor(const std::vector<int>& puzzle, int cell) {
  int row = rowOf(cell);
  int col = colOf(cell);

  for (int row2 = std::max(0,row-1);
       row2 < std::min(numRows,row+2); row2++) {
    for (int col2 = std::max(0,col-1);
         col2 < std::min(numCols,col+2); col2++) {
      if (row2 == row && col2 == col) {
        continue;
      }
      if (puzzle[ind(row2,col2)] != -1) {
        return true;
      }
    }
  }
  return false;
}


// After hiding one clue, only that square and its eight neighbors can newly
// lose their final visible-clue neighbor.
bool affectedUnknownsStillTouchAClue(const std::vector<int>& puzzle, int cell) {
  if (!requireEveryUnknownAdjacentToClue) {
    return true;
  }

  int row = rowOf(cell);
  int col = colOf(cell);
  for (int row2 = std::max(0,row-1);
       row2 < std::min(numRows,row+2); row2++) {
    for (int col2 = std::max(0,col-1);
         col2 < std::min(numCols,col+2); col2++) {
      int there = ind(row2,col2);
      if (puzzle[there] == -1 && !hasVisibleClueNeighbor(puzzle, there)) {
        return false;
      }
    }
  }
  return true;
}


struct Region {
  int top = 0;
  int left = 0;
  int rows = 0;
  int cols = 0;
};


bool inRegion(int cell, const Region& region) {
  int row = rowOf(cell);
  int col = colOf(cell);
  return row >= region.top && row < region.top + region.rows &&
         col >= region.left && col < region.left + region.cols;
}


Region chooseMutationRegion(std::mt19937& rng) {
  Region region;
  region.rows = std::min(numRows, mutationRegionRows);
  region.cols = std::min(numCols, mutationRegionCols);

  std::uniform_int_distribution<int> topDist(0, numRows - region.rows);
  std::uniform_int_distribution<int> leftDist(0, numCols - region.cols);
  region.top = topDist(rng);
  region.left = leftDist(rng);
  return region;
}


int visibleClueCount(const std::vector<int>& puzzle) {
  int answer = 0;
  for (int x : puzzle) {
    if (x != -1) {
      answer++;
    }
  }
  return answer;
}


std::string cluePatternKey(const std::vector<int>& puzzle) {
  std::string answer;
  answer.reserve(size);
  for (int x : puzzle) {
    answer.push_back(x == -1 ? '0' : '1');
  }
  return answer;
}


int cluePatternDistance(const std::vector<int>& a,
                        const std::vector<int>& b) {
  int answer = 0;
  for (int i = 0; i < size; i++) {
    if ((a[i] == -1) != (b[i] == -1)) {
      answer++;
    }
  }
  return answer;
}


// One greedy pass is enough to make the puzzle clue-minimal under the current
// rules. If removing a clue creates extra solutions, removing still more clues
// later cannot restore uniqueness.
int minimizePuzzle(std::vector<int>& puzzle,
                   std::mt19937& rng,
                   const Region* focusRegion=nullptr) {

  std::vector<int> inside;
  std::vector<int> outside;
  inside.reserve(size);
  outside.reserve(size);

  for (int i = 0; i < size; i++) {
    if (puzzle[i] == -1) {
      continue;
    }

    if (focusRegion && inRegion(i, *focusRegion)) {
      inside.push_back(i);
    } else {
      outside.push_back(i);
    }
  }

  std::shuffle(inside.begin(), inside.end(), rng);
  std::shuffle(outside.begin(), outside.end(), rng);

  std::vector<int> removalOrder;
  removalOrder.reserve(inside.size() + outside.size());

  if (focusRegion && regionCluesFirstInRemovalOrder) {
    removalOrder.insert(removalOrder.end(), inside.begin(), inside.end());
    removalOrder.insert(removalOrder.end(), outside.begin(), outside.end());
  } else {
    removalOrder.insert(removalOrder.end(), inside.begin(), inside.end());
    removalOrder.insert(removalOrder.end(), outside.begin(), outside.end());
    std::shuffle(removalOrder.begin(), removalOrder.end(), rng);
  }

  int removed = 0;
  for (int cell : removalOrder) {
    int clue = puzzle[cell];
    puzzle[cell] = -1;

    if (!affectedUnknownsStillTouchAClue(puzzle, cell)) {
      puzzle[cell] = clue;
      continue;
    }

    int count = countSolutionsIncrementalSat(puzzle);
    if (count == 1) {
      removed++;
    } else {
      puzzle[cell] = clue;
    }
  }

  return removed;
}


int numberToRestore(int available,
                    double fraction,
                    int minimumWhenAvailable) {
  if (available <= 0) {
    return 0;
  }

  int answer = int(std::round(fraction * available));
  answer = std::max(answer, minimumWhenAvailable);
  return std::min(answer, available);
}


struct MutationInfo {
  Region region;
  int restoredInside = 0;
  int restoredOutside = 0;
  int removedDuringMinimization = 0;
  int parentDistance = 0;
};


std::vector<int> makeMutatedChild(const std::vector<int>& parent,
                                  const std::vector<int>& fullPuzzle,
                                  std::mt19937& rng,
                                  MutationInfo& info) {
  std::vector<int> child = parent;
  info.region = chooseMutationRegion(rng);

  std::vector<int> hiddenInside;
  std::vector<int> hiddenOutside;
  hiddenInside.reserve(size);
  hiddenOutside.reserve(size);

  for (int i = 0; i < size; i++) {
    if (fullPuzzle[i] == -1 || child[i] != -1) {
      continue;   // mine, or already a visible clue
    }

    if (inRegion(i, info.region)) {
      hiddenInside.push_back(i);
    } else {
      hiddenOutside.push_back(i);
    }
  }

  std::shuffle(hiddenInside.begin(), hiddenInside.end(), rng);
  std::shuffle(hiddenOutside.begin(), hiddenOutside.end(), rng);

  info.restoredInside =
      numberToRestore(int(hiddenInside.size()), insideRestoreFraction,
                      minimumInsideCluesToRestore);
  info.restoredOutside =
      numberToRestore(int(hiddenOutside.size()), outsideRestoreFraction, 0);

  for (int i = 0; i < info.restoredInside; i++) {
    int cell = hiddenInside[i];
    child[cell] = fullPuzzle[cell];
  }
  for (int i = 0; i < info.restoredOutside; i++) {
    int cell = hiddenOutside[i];
    child[cell] = fullPuzzle[cell];
  }

  info.removedDuringMinimization =
      minimizePuzzle(child, rng, &info.region);
  info.parentDistance = cluePatternDistance(parent, child);
  return child;
}


int64_t selectedPropagationScore(const FreshSatResult& sat) {
  if (satDifficultyPropagationMode == 0) {
    return sat.first.propagations;
  }
  if (satDifficultyPropagationMode == 1) {
    return sat.second.propagations;
  }
  if (satDifficultyPropagationMode == 2) {
    return sat.total.propagations;
  }

  std::cerr << "satDifficultyPropagationMode must be 0, 1, or 2.\n";
  std::exit(1);
}


const char* propagationModeName() {
  if (satDifficultyPropagationMode == 0) {
    return "raw_sat_first_propagations";
  }
  if (satDifficultyPropagationMode == 1) {
    return "raw_sat_second_propagations";
  }
  return "raw_sat_total_propagations";
}


struct Candidate {
  std::vector<int> puzzle;
  int clueCount = 0;
  int generation = 0;

  int64_t satFirstPropagations = -1;
  int64_t satSecondPropagations = -1;
  int64_t satTotalPropagations = -1;
  int64_t satScore = -1;

  bool recursiveScored = false;
  OldSolverResult recursive;

  MutationInfo mutation;
};


void scoreCandidateWithFreshSat(Candidate& candidate) {
  FreshSatResult sat = solveFreshSatRaw(candidate.puzzle);

  if (sat.solCount != 1) {
    std::cerr << "Fresh SAT disagreed with incremental SAT: candidate count = "
              << sat.solCount << "\n";
    printPuzzle(candidate.puzzle);
    std::exit(1);
  }

  candidate.satFirstPropagations = sat.first.propagations;
  candidate.satSecondPropagations = sat.second.propagations;
  candidate.satTotalPropagations = sat.total.propagations;
  candidate.satScore = selectedPropagationScore(sat);

  if (candidate.satScore < 0) {
    std::cerr << "CaDiCaL propagation statistics are unavailable.\n";
    std::exit(1);
  }
}


void scoreCandidateRecursively(Candidate& candidate) {
  candidate.recursive = runRecursiveDifficulty(candidate.puzzle);
  candidate.recursiveScored = true;

  if (candidate.recursive.solCount != 1) {
    std::cerr << "Recursive solver disagreed with SAT: candidate count = "
              << candidate.recursive.solCount << "\n";
    printPuzzle(candidate.puzzle);
    std::exit(1);
  }
}


bool betterSatCandidate(const Candidate& a, const Candidate& b) {
  if (a.satScore != b.satScore) {
    return a.satScore > b.satScore;
  }
  if (a.satTotalPropagations != b.satTotalPropagations) {
    return a.satTotalPropagations > b.satTotalPropagations;
  }
  if (a.satFirstPropagations != b.satFirstPropagations) {
    return a.satFirstPropagations > b.satFirstPropagations;
  }
  return a.puzzle < b.puzzle;
}


bool betterCandidate(const Candidate& a, const Candidate& b) {
  if (useRecursiveDifficulty) {
    if (a.recursiveScored != b.recursiveScored) {
      return a.recursiveScored;
    }

    if (a.recursive.firstSolutionDepth != b.recursive.firstSolutionDepth) {
      return a.recursive.firstSolutionDepth > b.recursive.firstSolutionDepth;
    }
    if (a.recursive.guessBranches != b.recursive.guessBranches) {
      return a.recursive.guessBranches > b.recursive.guessBranches;
    }
    if (a.recursive.finalBranches != b.recursive.finalBranches) {
      return a.recursive.finalBranches > b.recursive.finalBranches;
    }
  }

  if (useSatDifficulty) {
    return betterSatCandidate(a, b);
  }
  return a.puzzle < b.puzzle;
}


void recursivelyScoreTopFraction(std::vector<Candidate>& candidates) {
  if (!useRecursiveDifficulty || candidates.empty()) {
    return;
  }

  int numberToScore;

  if (!useSatDifficulty) {
    // Without a SAT difficulty score there is no cheap prefilter, so recursive
    // scoring must be done for every candidate.
    numberToScore = int(candidates.size());
  } else {
    std::sort(candidates.begin(), candidates.end(), betterSatCandidate);

    if (fractionToRecursiveScore >= 1.0) {
      numberToScore = int(candidates.size());
    } else {
      numberToScore = int(std::ceil(
          fractionToRecursiveScore * candidates.size()
      ));
      numberToScore = std::max(1, numberToScore);
    }
  }

  for (int i = 0; i < numberToScore; i++) {
    scoreCandidateRecursively(candidates[i]);
  }
}


std::vector<Candidate> candidatesEligibleForPool(
    std::vector<Candidate>& candidates) {
  if (!useRecursiveDifficulty) {
    return candidates;
  }

  std::vector<Candidate> answer;
  for (Candidate& candidate : candidates) {
    if (candidate.recursiveScored) {
      answer.push_back(std::move(candidate));
    }
  }
  return answer;
}


void sortAndTrimPool(std::vector<Candidate>& pool) {
  std::sort(pool.begin(), pool.end(), betterCandidate);
  if (int(pool.size()) > poolSize) {
    pool.resize(poolSize);
  }
}


void printCandidateScore(const Candidate& candidate) {
  if (useSatDifficulty) {
    std::cout << propagationModeName() << " = " << candidate.satScore
              << ", log10(score+1) = "
              << std::log10(double(candidate.satScore) + 1.0)
              << ", ";
  }

  std::cout << "clues = " << candidate.clueCount;

  if (candidate.recursiveScored) {
    std::cout << ", recursive depth = "
              << candidate.recursive.firstSolutionDepth
              << ", recursive guess branches = "
              << candidate.recursive.guessBranches
              << ", recursive final branches = "
              << candidate.recursive.finalBranches
              << ", recursive time = "
              << candidate.recursive.time;
  }
}


void printSettings() {
  std::cout << "Evolution settings\n";
  std::cout << "board = " << numRows << " by " << numCols << "\n";
  std::cout << "generations = " << numberOfGenerations << "\n";
  std::cout << "pool size = " << poolSize << "\n";
  std::cout << "initial puzzle trials = " << initialPuzzleTrials << "\n";
  std::cout << "parents per generation = " << parentsPerGeneration << "\n";
  std::cout << "children per parent = " << childrenPerParent << "\n";
  std::cout << "mutation region = "
            << std::min(numRows, mutationRegionRows) << " by "
            << std::min(numCols, mutationRegionCols) << "\n";
  std::cout << "inside restore fraction = " << insideRestoreFraction << "\n";
  std::cout << "outside restore fraction = " << outsideRestoreFraction << "\n";
  std::cout << "minimum inside clues restored = "
            << minimumInsideCluesToRestore << "\n";
  std::cout << "SAT difficulty metric = " << propagationModeName() << "\n";
  std::cout << "use SAT difficulty = " << useSatDifficulty << "\n";
  std::cout << "use recursive difficulty = " << useRecursiveDifficulty << "\n";
  if (useRecursiveDifficulty) {
    std::cout << "fraction recursively scored = "
              << fractionToRecursiveScore << "\n";
  }
  std::cout << "\n";
}


void validateParameters() {
  if (numRows <= 0 || numCols <= 0) {
    std::cerr << "Puzzle dimensions must be positive.\n";
    std::exit(1);
  }
  if (mutationRegionRows <= 0 || mutationRegionCols <= 0) {
    std::cerr << "Mutation-region dimensions must be positive.\n";
    std::exit(1);
  }
  if (insideRestoreFraction < 0 || insideRestoreFraction > 1 ||
      outsideRestoreFraction < 0 || outsideRestoreFraction > 1) {
    std::cerr << "Restore fractions must be between 0 and 1.\n";
    std::exit(1);
  }
  if (poolSize <= 0 || initialPuzzleTrials <= 0 ||
      parentsPerGeneration <= 0 || childrenPerParent <= 0) {
    std::cerr << "Pool and trial counts must be positive.\n";
    std::exit(1);
  }
  if (!useSatDifficulty && !useRecursiveDifficulty) {
    std::cerr << "At least one difficulty metric must be enabled.\n";
    std::exit(1);
  }
  if (fractionToRecursiveScore <= 0 || fractionToRecursiveScore > 1) {
    std::cerr << "fractionToRecursiveScore must be in (0,1].\n";
    std::exit(1);
  }
  if (!useSatDifficulty && useRecursiveDifficulty &&
      fractionToRecursiveScore != 1.0) {
    std::cerr << "Without SAT difficulty, fractionToRecursiveScore must be 1.\n";
    std::exit(1);
  }
  if (satDifficultyPropagationMode < 0 ||
      satDifficultyPropagationMode > 2) {
    std::cerr << "satDifficultyPropagationMode must be 0, 1, or 2.\n";
    std::exit(1);
  }

  if (useSatDifficulty) {
    CaDiCaL::Solver testSolver;
    if (testSolver.get_statistic_value("propagations") < 0) {
      std::cerr << "This CaDiCaL version does not expose propagation statistics.\n";
      std::exit(1);
    }
  }
}


void makeFullPuzzle(std::mt19937& rng,
                    std::vector<int>& fullPuzzle,
                    std::vector<int>& isFlag) {
  const double targetFlagFraction = 0.25;
  const double relativeTolerance = 0.05;

  double target = targetFlagFraction * size;
  int minimumFlags =
      int(std::ceil(target * (1.0 - relativeTolerance)));
  int maximumFlags =
      int(std::floor(target * (1.0 + relativeTolerance)));

  // For tiny boards, no integer may lie inside the requested relative range.
  if (minimumFlags > maximumFlags) {
    minimumFlags = maximumFlags =
        std::max(0, std::min(size, int(std::round(target))));
  }

  std::uniform_int_distribution<int> flagDist(0, 3);
  int flagCount = -1;
  bool everyMineTouchesASafeSquare = false;

  while (flagCount < minimumFlags || flagCount > maximumFlags ||
         !everyMineTouchesASafeSquare) {
    flagCount = 0;
    for (int i = 0; i < size; i++) {
      isFlag[i] = flagDist(rng) == 0;
      flagCount += isFlag[i];
    }

    everyMineTouchesASafeSquare = true;
    for (int cell = 0; cell < size && everyMineTouchesASafeSquare; cell++) {
      if (!isFlag[cell]) {
        continue;
      }

      bool touchesSafe = false;
      int row = rowOf(cell);
      int col = colOf(cell);
      for (int row2 = std::max(0,row-1);
           row2 < std::min(numRows,row+2); row2++) {
        for (int col2 = std::max(0,col-1);
             col2 < std::min(numCols,col+2); col2++) {
          if (!isFlag[ind(row2,col2)]) {
            touchesSafe = true;
          }
        }
      }

      if (!touchesSafe) {
        everyMineTouchesASafeSquare = false;
      }
    }
  }

  fullPuzzle.assign(size, 0);
  for (int row = 0; row < numRows; row++) {
    for (int col = 0; col < numCols; col++) {
      int here = ind(row,col);
      if (isFlag[here]) {
        fullPuzzle[here] = -1;
        continue;
      }

      int clue = 0;
      for (int row2 = std::max(0,row-1);
           row2 < std::min(numRows,row+2); row2++) {
        for (int col2 = std::max(0,col-1);
             col2 < std::min(numCols,col+2); col2++) {
          clue += isFlag[ind(row2,col2)];
        }
      }
      fullPuzzle[here] = clue;
    }
  }
}


Candidate makeScoredCandidate(const std::vector<int>& puzzle,
                              int generation,
                              const MutationInfo& mutation) {
  Candidate candidate;
  candidate.puzzle = puzzle;
  candidate.clueCount = visibleClueCount(puzzle);
  candidate.generation = generation;
  candidate.mutation = mutation;

  // Whenever SAT is used for difficulty, this builds a fresh solver. The
  // incremental generation solver's learned information never affects scores.
  if (useSatDifficulty) {
    scoreCandidateWithFreshSat(candidate);
  }
  return candidate;
}


void printFinalCandidate(const Candidate& candidate, int rank) {
  std::cout << "\n";
  std::cout << "rank = " << rank << "\n";
  std::cout << "created in generation = " << candidate.generation << "\n";
  printCandidateScore(candidate);
  std::cout << "\n";
  if (useSatDifficulty) {
    std::cout << "raw_sat_first_propagations = "
              << candidate.satFirstPropagations << "\n";
    std::cout << "raw_sat_second_propagations = "
              << candidate.satSecondPropagations << "\n";
    std::cout << "raw_sat_total_propagations = "
              << candidate.satTotalPropagations << "\n";
  }

  if (candidate.generation > 0) {
    std::cout << "last mutation region = "
              << candidate.mutation.region.rows << " by "
              << candidate.mutation.region.cols << " at ("
              << candidate.mutation.region.top << ", "
              << candidate.mutation.region.left << ")\n";
    std::cout << "last mutation restored inside/outside = "
              << candidate.mutation.restoredInside << "/"
              << candidate.mutation.restoredOutside << "\n";
    std::cout << "parent clue-pattern distance = "
              << candidate.mutation.parentDistance << "\n";
  }

  printPuzzle(candidate.puzzle);

  std::vector<int> printable = candidate.puzzle;
  for (int& x : printable) {
    if (x == -1) {
      x = 9;
    }
  }
  std::cout << arrayToString(printable) << "\n";
}


//////// main


int main() {
  validateParameters();

  auto start = std::chrono::steady_clock::now();

  if (seed < 0) {
    std::random_device rd;
    seed = int(rd() % 10000000);
  }
  std::mt19937 rng(seed);

  printSettings();
  std::cout << "seed = " << seed << "\n\n";

  std::vector<int> fullPuzzle(size, 0);
  std::vector<int> isFlag(size, 0);
  makeFullPuzzle(rng, fullPuzzle, isFlag);

  initializeIncrementalSatSolver(fullPuzzle);

  if (countSolutionsIncrementalSat(fullPuzzle) != 1) {
    std::cerr << "The fully exposed puzzle was unexpectedly not unique.\n";
    return 1;
  }

  std::unordered_set<std::string> seen;
  std::vector<Candidate> initialCandidates;

  std::cout << "Making initial clue-minimal difficulty=2 puzzles...\n";

  for (int trial = 0; trial < initialPuzzleTrials; trial++) {
    std::vector<int> puzzle = fullPuzzle;
    minimizePuzzle(puzzle, rng);

    std::string key = cluePatternKey(puzzle);
    if (!seen.insert(key).second) {
      std::cout << "initial trial " << trial + 1
                << ": duplicate\n" << std::flush;
      continue;
    }

    int logicStatus = runNonRecursiveSolver(puzzle);
    if (logicStatus == -1) {
      std::cerr << "Non-recursive solver found a contradiction in a unique puzzle.\n";
      printPuzzle(puzzle);
      return 1;
    }
    if (logicStatus == 1) {
      std::cout << "initial trial " << trial + 1
                << ": rejected because non-recursive logic solved it\n"
                << std::flush;
      continue;
    }

    MutationInfo noMutation;
    Candidate candidate = makeScoredCandidate(puzzle, 0, noMutation);
    initialCandidates.push_back(std::move(candidate));

    std::cout << "initial trial " << trial + 1 << ": accepted, ";
    printCandidateScore(initialCandidates.back());
    std::cout << "\n" << std::flush;
  }

  if (initialCandidates.empty()) {
    std::cerr << "\nNo difficulty=2 initial puzzle was found for this fixed "
                 "solution. Increase initialPuzzleTrials or use another seed.\n";
    return 1;
  }

  recursivelyScoreTopFraction(initialCandidates);
  std::vector<Candidate> pool =
      candidatesEligibleForPool(initialCandidates);
  sortAndTrimPool(pool);

  if (pool.empty()) {
    std::cerr << "No initial candidate remained after recursive scoring.\n";
    return 1;
  }

  std::cout << "\nInitial pool size = " << pool.size() << "\n";
  std::cout << "Initial best: ";
  printCandidateScore(pool.front());
  std::cout << "\n\n" << std::flush;

  for (int generation = 1; generation <= numberOfGenerations; generation++) {
    sortAndTrimPool(pool);

    int parentCount =
        std::min(parentsPerGeneration, int(pool.size()));
    int attempted = parentCount * childrenPerParent;
    int duplicates = 0;
    int logicSolved = 0;
    int newHardCandidates = 0;

    std::vector<Candidate> children;
    children.reserve(attempted);

    for (int parentIndex = 0; parentIndex < parentCount; parentIndex++) {
      const Candidate& parent = pool[parentIndex];

      for (int childNumber = 0;
           childNumber < childrenPerParent; childNumber++) {
        MutationInfo mutation;
        std::vector<int> childPuzzle =
            makeMutatedChild(parent.puzzle, fullPuzzle, rng, mutation);

        std::string key = cluePatternKey(childPuzzle);
        if (!seen.insert(key).second) {
          duplicates++;
          continue;
        }

        int logicStatus = runNonRecursiveSolver(childPuzzle);
        if (logicStatus == -1) {
          std::cerr << "Non-recursive solver found a contradiction in a "
                       "SAT-unique child.\n";
          printPuzzle(childPuzzle);
          return 1;
        }
        if (logicStatus == 1) {
          logicSolved++;
          continue;
        }

        Candidate child =
            makeScoredCandidate(childPuzzle, generation, mutation);
        children.push_back(std::move(child));
        newHardCandidates++;
      }
    }

    recursivelyScoreTopFraction(children);
    std::vector<Candidate> eligibleChildren =
        candidatesEligibleForPool(children);
    int recursivelyScored = int(eligibleChildren.size());

    std::string oldBestKey = cluePatternKey(pool.front().puzzle);

    for (Candidate& child : eligibleChildren) {
      pool.push_back(std::move(child));
    }
    sortAndTrimPool(pool);

    bool bestChanged = cluePatternKey(pool.front().puzzle) != oldBestKey;

    std::cout << "generation " << generation
              << ": attempted = " << attempted
              << ", duplicates = " << duplicates
              << ", logic-solved = " << logicSolved
              << ", new difficulty=2 = " << newHardCandidates;

    if (useRecursiveDifficulty) {
      std::cout << ", recursively scored = " << recursivelyScored;
    }

    std::cout << ", pool = " << pool.size()
              << ", best changed = " << bestChanged << "\n";
    std::cout << "  best: ";
    printCandidateScore(pool.front());
    std::cout << "\n" << std::flush;
  }

  sortAndTrimPool(pool);

  std::cout << "\n";
  std::cout << "Evolution complete\n";
  std::cout << "incremental SAT uniqueness checks = "
            << incrementalSatChecks << "\n";
  std::cout << "different clue patterns examined = "
            << seen.size() << "\n";
  std::cout << "final pool size = " << pool.size() << "\n";

  int numberToPrint =
      std::min(finalPuzzlesToPrint, int(pool.size()));
  for (int i = 0; i < numberToPrint; i++) {
    printFinalCandidate(pool[i], i + 1);
  }

  std::cout << "\nFixed underlying solution\n";
  printPuzzle(isFlag);
  std::string solutionString = arrayToString(isFlag);
  std::cout << "hash = " << sha256(solutionString) << "\n";

  auto end = std::chrono::steady_clock::now();
  double elapsed =
      std::chrono::duration<double>(end - start).count();
  std::cout << "seed = " << seed << "\n";
  std::cout << elapsed << " s\n\n";

  return 0;
}
