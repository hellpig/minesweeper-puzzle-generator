
/*
 Generate minesweeper puzzles and solution SHA-256 hash similar to those at
   https://www.puzzle-minesweeper.com/
 Feel free to change the distribution of number of flags
   in the code via when isFlag is set

 Build CaDiCaL once with its own ./configure && make, then compile and run with...
   g++ -std=c++17 -O3 gen_sat.cpp -Icadical/src cadical/build/libcadical.a -o gen_sat
   ./gen_sat
*/



#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "cadical.hpp"




const int numRows = 50;
const int numCols = 50;
const int size = numRows * numCols;
// If you make this too small, the target range for flag count may not be
//   possible, and the code will try forever.

int difficulty = 2;   // 0, 1, or 2
// 0 is easy. Only one-number-at-a-time logic is needed.
// 1 is medium. Two-numbers-at-a-time logic is also needed.
// 2 is hard. Extra logic is needed (done via guessing in this code)
//
// If difficulty > 0, this is the ALLOWED difficulty. There is no guarantee that it will
//   be this hard, but it won't be harder.
// The code will check if the target difficulty was obtained.


long long branchLimitBeforeSat = 200;
// Only used when difficulty == 2.
// -1 means pure incremental SAT.
//  0 means use the old non-recursive solver first, then SAT if guessing is needed.
//  N means allow N old recursive branches before falling back to SAT.
//  A huge number, such as 10000000000LL, gives essentially the old behavior.
//
// If the old solver hits the branch limit, SAT starts fresh from puzzle[].
// It does not reuse partial work from the old solver. However, the SAT solver
// itself is incremental: its clauses and learned information are retained
// between candidate puzzles.

bool useSatAfterFirstFallback = true;
// If true, then after one candidate needs SAT, all later candidates start with
// SAT immediately. If false, the old solver still gets a first try each time.
// Because the SAT solver is incremental, keeping the switch permanent may help.
// Timing can determine whether this should be true for a given puzzle size.
//
// Another possible design would keep SAT active only when the candidate removal
// that triggered SAT is accepted. If that removal is rejected and the clue is
// restored, the code could return to trying the old solver first. This may help
// when rejected removals are easy for the old solver to prove non-unique.
// The current code instead keeps SAT active even after a rejected removal,
// because the incremental SAT solver retains learned information that may help
// with later, closely related candidate puzzles.


int seed = -1;  // set to -1 to get a random seed

// The only things that use this seed are...
//   flagDist(rng), which randomly places the initial flags
//   std::shuffle(remainingToTry), which randomizes clue-removal order
// Note: the same seed will not reproduce the old Python version because
//   NumPy and C++ use different random number generators.


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


// Minimal SHA-256 implementation so the generator has minimal dependencies.
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

// created to be a better branch count for when to switch to SAT
long long branchCount = 0;

// hitBranchLimit is created so we know why recursive_func returns.
// branchCount >= branchLimitBeforeSat might be true and also not why the function returned.
bool hitBranchLimit = false;

// Once true, all later candidates start with the same incremental SAT solver.
bool satFallbackActive = false;


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


//////// incremental SAT solver

// There is one mine variable for every square, including squares that can be clues.
// If a clue is visible, its selector activates both its clue constraint and the
// requirement that the clue square itself is not a mine.
CaDiCaL::Solver satSolver;
std::vector<int> satMineVars(size, 0);
std::vector<int> satClueSelectors(size, 0);
int nextSatVar = 1;


void addSatClause(const std::vector<int>& clause) {
  satSolver.clause(clause);
}


void addGuardedExactly(int selector, const SmallSet& vars, int wanted) {
  int n = int(vars.size());

  if (wanted < 0 || wanted > n) {
    // This clue can never be active.
    satSolver.clause(-selector);
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
    addSatClause(clause);
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
    addSatClause(clause);
    return true;
  });
}


void initializeSatSolver(const std::vector<int>& puzzle) {

  int clueCount = 0;
  for (int i = 0; i < size; i++) {
    if (puzzle[i] != -1) {
      clueCount++;
    }
  }

  // Explicitly declare all user variables before adding clauses. This prevents
  // CaDiCaL's factor preprocessing from using an index that we later try to use.
  satSolver.declare_more_variables(size + clueCount);

  for (int i = 0; i < size; i++) {
    satMineVars[i] = nextSatVar++;
  }

  for (int i = 0; i < size; i++) {
    if (puzzle[i] == -1) {   // the original flags can never be visible clues
      continue;
    }

    int selector = nextSatVar++;
    satClueSelectors[i] = selector;

    // A visible clue square is clear.
    satSolver.clause(-selector, -satMineVars[i]);

    SmallSet neighborVars;
    neighborVars.reserve(8);
    int row = rowOf(i);
    int col = colOf(i);
    for (int row2 = std::max(0,row-1); row2 < std::min(numRows,row+2); row2++) {
      for (int col2 = std::max(0,col-1); col2 < std::min(numCols,col+2); col2++) {
        if (row2==row && col2==col) {
          continue;
        }
        neighborVars.push_back(satMineVars[ind(row2,col2)]);
      }
    }

    addGuardedExactly(selector, neighborVars, puzzle[i]);
  }
}


void assumeSatPuzzle(const std::vector<int>& puzzle) {
  for (int i = 0; i < size; i++) {
    if (!satClueSelectors[i]) {
      continue;
    }
    if (puzzle[i] == -1) {
      satSolver.assume(-satClueSelectors[i]);
    } else {
      satSolver.assume(satClueSelectors[i]);
    }
  }
}


int count_solutions_sat(const std::vector<int>& puzzle) {

  assumeSatPuzzle(puzzle);
  int result = satSolver.solve();

  if (result == CaDiCaL::UNSATISFIABLE) {
    return 0;
  }
  if (result != CaDiCaL::SATISFIABLE) {
    std::cerr << "SAT solver returned UNKNOWN!\n";
    std::exit(1);
  }


  // The constraint clause exists only for the next solve. Therefore, unlike a
  // normal added blocking clause, it does not accumulate inside the solver.
  // It says that the next solution must differ from the first one somewhere.

  // Read the complete solution before calling constrain(). Calling constrain()
  // changes the solver state, after which val() can no longer be used.
  std::vector<bool> firstSolution(size);
  for (int i = 0; i < size; i++) {
    firstSolution[i] = satSolver.val(satMineVars[i]) > 0;
  }

  for (int i = 0; i < size; i++) {
    if (firstSolution[i]) {
      satSolver.constrain(-satMineVars[i]);
    } else {
      satSolver.constrain(satMineVars[i]);
    }
  }
  satSolver.constrain(0);


  // Assumptions last for only one solve, so add them again.
  assumeSatPuzzle(puzzle);
  result = satSolver.solve();

  if (result == CaDiCaL::SATISFIABLE) {
    return 2;
  }
  if (result == CaDiCaL::UNSATISFIABLE) {
    return 1;
  }

  std::cerr << "SAT solver returned UNKNOWN!\n";
  std::exit(1);
}


void recursive_func(std::vector<int>& remaining,
                    std::vector<int>& flags,
                    std::vector<SmallSet>& unknowns,
                    std::vector<SmallSet>& connectionsPairs,
                    const std::vector<SmallSet>& connections,
                    bool printDepth=false,
                    int depth=0,
                    bool useBranchLimit=true) {

  // solve whatever we can solve without guessing. This greatly speeds things up!
  if (solve(remaining, flags, unknowns, connectionsPairs, connections) == -1) {
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

  if (difficulty < 2) {
    return;
  }

  if (useBranchLimit && branchLimitBeforeSat >= 0) {
    if (branchCount >= branchLimitBeforeSat) {
      hitBranchLimit = true;
      return;
    }
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

    // count branches
    if (useBranchLimit && branchLimitBeforeSat >= 0) {
      if (branchCount >= branchLimitBeforeSat) {
        hitBranchLimit = true;
        return false;
      }
      branchCount += 1;
    }

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

    recursive_func(r2, f2, u2, c2, connections, printDepth, depth+1, useBranchLimit);
    // hitBranchLimit may still be true from generation, but it must be ignored
    // when branch limiting is disabled for the final difficulty analysis.
    return !useBranchLimit || !hitBranchLimit;
  });
}


//////// wrapper created in order to have a hybrid (recursive_func and SAT) approach
// This is not needed if difficulty < 2.
// This function is only called on one line of code.

int count_solutions_hybrid(const std::vector<int>& puzzle,
                           std::vector<int>& remaining,
                           std::vector<int>& flags,
                           std::vector<SmallSet>& unknowns,
                           std::vector<SmallSet>& connectionsPairs,
                           const std::vector<SmallSet>& connections) {

  if (difficulty == 2 && (branchLimitBeforeSat < 0 || satFallbackActive)) {
    return count_solutions_sat(puzzle);   // run the incremental SAT solver instead
  }

  solCount = 0;
  branchCount = 0;
  hitBranchLimit = false;

  recursive_func(remaining, flags, unknowns, connectionsPairs, connections);

  if (difficulty == 2 && hitBranchLimit) {
    if (useSatAfterFirstFallback) {
      satFallbackActive = true;
    }
    return count_solutions_sat(puzzle);   // use SAT because recursive_func stopped early
  }

  return solCount;
}




////////  main

int main() {

    auto start = std::chrono::steady_clock::now();

    if (seed < 0) {
      std::random_device rd;
      seed = int(rd() % 10000000);   // feel free to increase high
    }
    std::mt19937 rng(seed);


    // -1 is unknowns
    // 0-8 are those numbers
    std::vector<int> puzzle(size, 0);

    // Add flags to, on average, 25% of the squares
    // Enforcing it to be between 20% and 30% of the squares
    double average = double(numRows * numCols) / 4.0;
    int flagNum = 0;
    std::vector<int> isFlag(size, 0);
    std::uniform_int_distribution<int> flagDist(0, 3);
    while (std::abs(flagNum - average) / average > .05) {   // feel free to change the .05
      flagNum = 0;
      for (int i = 0; i < size; i++) {
        isFlag[i] = flagDist(rng) == 0;
        flagNum += isFlag[i];
      }
    }
    for (int i = 0; i < size; i++) {
      if (isFlag[i]) {
        puzzle[i] = -1;
      }
    }

    // Change all 0s to numbers and make remainingToTry[]
    std::vector<int> remainingToTry;
    remainingToTry.reserve(size);
    for (int i = 0; i < numRows; i++) {
      for (int j = 0; j < numCols; j++) {
        if (isFlag[ind(i,j)]) {
          continue;
        }

        for (int i2 = std::max(0,i-1); i2 < std::min(numRows,i+2); i2++) {
          for (int j2 = std::max(0,j-1); j2 < std::min(numCols,j+2); j2++) {
            if (isFlag[ind(i2,j2)]) {
              puzzle[ind(i,j)] += 1;
            }
          }
        }

        remainingToTry.push_back(ind(i,j));
      }
    }

    std::shuffle(remainingToTry.begin(), remainingToTry.end(), rng);

    if (difficulty == 2) {
      initializeSatSolver(puzzle);
    }



    // make the base ("0") data structures to be later copied to become the non-0 ones

    // locations of numbers in a 3 by 3 box surrounding each unknown
    std::vector<SmallSet> connections0(size);
    // The solver does not change this variable, though the generator does.

    // locations of numbers in a 5 by 5 box surrounding each number
    //   but the 4 corners are ignored
    // Repeats of pairs are not listed (the other box will not connect back)
    std::vector<SmallSet> connectionsPairs0(size);

    // number of flags needed to be placed.
    //   int32 is faster than int8
    std::vector<int> flags0(size, 0);

    // unknowns surrounding each number
    std::vector<SmallSet> unknowns0(size);

    // remaining numbers to be looped over
    // For now, all numbers on board
    std::vector<int> remaining0;
    remaining0.reserve(size);

    for (int i = 0; i < numRows; i++) {
        for (int j = 0; j < numCols; j++) {

            int here = ind(i,j);

            // make connections0[]
            if (puzzle[here] == -1) {   // fun fact: right now, all -1 are flags

                SmallSet c;
                c.reserve(8);
                for (int i2 = std::max(0,i-1); i2 < std::min(numRows,i+2); i2++) {
                  for (int j2 = std::max(0,j-1); j2 < std::min(numCols,j+2); j2++) {
                    if (i2==i && j2==j) {
                      continue;
                    }
                    if (puzzle[ind(i2,j2)] != -1) {
                      c.push_back(ind(i2,j2));
                    }
                  }
                }
                connections0[here] = c;

                continue;
            }

            // make remaining0[] and flags0[]
            remaining0.push_back(here);
            flags0[here] = puzzle[here];

            // find unknowns0 surrounding each number
            SmallSet u;
            for (int i2 = std::max(0,i-1); i2 < std::min(numRows,i+2); i2++) {
              for (int j2 = std::max(0,j-1); j2 < std::min(numCols,j+2); j2++) {
                if (puzzle[ind(i2,j2)] == -1) {
                  u.push_back(ind(i2,j2));
                }
              }
            }
            unknowns0[here] = u;

            // find connectionsPairs0
            SmallSet c;
            c.reserve(20);
            for (int i2 = i; i2 < std::min(numRows,i+3); i2++) {
              for (int j2 = std::max(0,j-2); j2 < std::min(numCols,j+3); j2++) {
                if (puzzle[ind(i2,j2)] != -1) {
                  if ((i2-i)!=2 || std::abs(j-j2)!=2) {  // remove 4 corners
                    if (i2>i || (i2==i && j2>j)) { // list each pair only once and do not list itself
                      c.push_back(ind(i2,j2));
                    }
                  }
                }
              }
            }
            connectionsPairs0[here] = c;
            // I use sets because of an (incomplete) attempt to remove elements of these,
            //   though it may be fastest to never try to alter connectionsPairs at all!
        }
    }




    // For each in remainingToTry[], try to replace with a -1.
    // If the puzzle still has 1 and only 1 solution, keep it a -1,
    //   else put it back.
    for (int loop = int(remainingToTry.size()); loop > 0; loop--) {
        int k = remainingToTry.back();
        remainingToTry.pop_back();
        int i = rowOf(k);
        int j = colOf(k);


        // make sure that all unknowns will have a number around them!

        // check surrounding unknowns
        bool stop = false;
        for (int c : unknowns0[k]) {
          if (connections0[c].size() < 2) {
            stop = true;
            break;
          }
        }
        if (stop) {
          continue;
        }

        // check the new unknown at k to make sure a number surrounds it
        stop = true;
        for (int i2 = std::max(0,i-1); i2 < std::min(numRows,i+2); i2++) {
          for (int j2 = std::max(0,j-1); j2 < std::min(numCols,j+2); j2++) {
            if (i2==i && j2==j) {
              continue;
            }
            if (puzzle[ind(i2,j2)] != -1) {
              stop = false;
              break;
            }
          }
        }
        if (stop) {
          continue;
        }



        // update the data structures
        // flags[k], unknowns[k], and connectionsPairs[k] do not need updating
        // Deep copy any part that could get changed by the solver (puzzle[] and connections[] are not changed).
        // Deep copy any part that will be reverted if removing k gives multiple solutions.

        // no copy at all!
        int num = puzzle[k];
        puzzle[k] = -1;

        // complete deep copy
        std::vector<int> remaining = remaining0;
        removeFromSet(remaining, k);

        // complete deep copy
        std::vector<int> flags = flags0;

        // only deep copy the changes
        std::vector<SmallSet> connections = connections0;  // shallow copy
        connections[k].clear();   // deep copy here
        for (int i2 = std::max(0,i-1); i2 < std::min(numRows,i+2); i2++) {
          for (int j2 = std::max(0,j-1); j2 < std::min(numCols,j+2); j2++) {
            if (i2==i && j2==j) {
              continue;
            }
            int here = ind(i2,j2);
            if (puzzle[here] == -1) {
              // copy, then remove k
              connections[here] = connections0[here];   // deep copy here
              removeFromSet(connections[here], k);
            } else {
              connections[k].push_back(here);
            }
          }
        }

        // essentially a complete deep copy because only unknowns[] at remaining matter
        std::vector<SmallSet> unknowns = unknowns0;
        for (int c : connections[k]) {  // connections[k] must first be updated
          addToSet(unknowns[c], k);
        }

        // essentially a complete deep copy because only connectionsPairs[] at remaining matter
        std::vector<SmallSet> connectionsPairs = connectionsPairs0;
        for (int i2 = std::max(0,i-2); i2 < i+1; i2++) {
          for (int j2 = std::max(0,j-2); j2 < std::min(numCols,j+3); j2++) {
            int here = ind(i2,j2);
            if (puzzle[here] != -1) {
              if ((i-i2)!=2 || std::abs(j-j2)!=2) {  // remove 4 corners
                if (i2<i || (i2==i && j2<j)) {
                  removeFromSet(connectionsPairs[here], k);
                }
              }
            }
          }
        }




        // do it!

        solCount = count_solutions_hybrid(puzzle, remaining, flags, unknowns, connectionsPairs, connections);


        /*
        // print progress info useful when making huge puzzles that take a long time to make
        // If all unknowns did not have a number around them, neither will print
        if (solCount != 1) {
          std::cout << "puzzle made no harder. " << remainingToTry.size() << " left\n";
        } else {
          std::cout << "puzzle made harder! " << remainingToTry.size() << " left\n";
        }
        */


        // update things

        if (solCount != 1) {
          puzzle[k] = num;   //put back number
        } else {

          // update "0" data structures

          removeFromSet(remaining0, k);

          connections0[k] = connections[k];
          for (int i2 = std::max(0,i-1); i2 < std::min(numRows,i+2); i2++) {
            for (int j2 = std::max(0,j-1); j2 < std::min(numCols,j+2); j2++) {
              if (i2==i && j2==j) {
                continue;
              }
              int here = ind(i2,j2);
              if (puzzle[here] == -1) {
                connections0[here] = connections[here];
              }
            }
          }

          for (int c : connections0[k]) {
            addToSet(unknowns0[c], k);
          }

          for (int i2 = std::max(0,i-2); i2 < i+1; i2++) {
            for (int j2 = std::max(0,j-2); j2 < std::min(numCols,j+3); j2++) {
              int here = ind(i2,j2);
              if (puzzle[here] != -1) {
                if ((i-i2)!=2 || std::abs(j-j2)!=2) {  // remove 4 corners
                  if (i2<i || (i2==i && j2<j)) {
                    removeFromSet(connectionsPairs0[here], k);
                  }
                }
              }
            }
          }
        }
    }



    std::cout << "\n";
    std::cout << "(" << numRows << ", " << numCols << ")\n";

    printPuzzle(puzzle);
    for (int& x : puzzle) {
      if (x == -1) {
        x = 9;
      }
    }
    std::cout << arrayToString(puzzle) << "\n";

    std::cout << "\n";
    printPuzzle(isFlag);
    std::string strFlat = arrayToString(isFlag);
    //std::cout << strFlat << "\n";
    std::cout << "hash = " << sha256(strFlat) << "\n";
    std::cout << "\n";



    if (difficulty) {   // have it run again on a smaller difficulty

      // make copies
      std::vector<int> remaining = remaining0;
      std::vector<int> flags = flags0;
      std::vector<SmallSet> unknowns = unknowns0;
      std::vector<SmallSet> connectionsPairs = connectionsPairs0;

      if (difficulty == 1) {

        difficulty = 0;
        solCount = 0;
        recursive_func(remaining, flags, unknowns, connectionsPairs, connections0);

        if (!remaining.size()) {  // if solved
          std::cout << "Target difficulty not reached!\n";
          std::cout << "difficulty = 0\n";
        }

        difficulty = 1;

      } else if (difficulty == 2) {

        difficulty = 1;
        solCount = 0;
        recursive_func(remaining, flags, unknowns, connectionsPairs, connections0);

        if (!remaining.size()) {  // if solved

          difficulty = 0;
          solCount = 0;
          recursive_func(remaining0, flags0, unknowns0, connectionsPairs0, connections0);

          std::cout << "Target difficulty not reached!\n";
          if (!remaining0.size()) {  // if solved
            std::cout << "difficulty = 0\n";
          } else {
            std::cout << "difficulty = 1\n";
          }

          difficulty = 2;

        } else {

          // Print depth and finalBranches
          // These are mostly meaningless but kinda interesting data.
          // They are meaningless because they depend on the exact order that
          //   numbers are traversed in the recursive solver. In the current state
          //   of the code, it is also unclear if the data are due
          //   to a bunch of easy situations or one very hard one or whatever.
          // Note that depth may go deeper for branches that did not find the solution.
          difficulty = 2;
          solCount = 0;
          finalBranches = 0;
          recursive_func(remaining0, flags0, unknowns0, connectionsPairs0, connections0, true, 0, false);
          std::cout << "finalBranches = " << finalBranches << "\n";

          //return 0;  // when I'm looking for the first difficulty=2 of a small puzzle size
        }
      }
    }

    auto end = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(end - start).count();
    std::cout << "seed = " << seed << "\n";
    std::cout << elapsed << " s\n";
    std::cout << "\n";

    return 0;
}
