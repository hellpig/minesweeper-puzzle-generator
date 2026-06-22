
# minesweeper-puzzle-generator
Generate Minesweeper puzzles with unique solutions from the easiest to the hardest possible difficulties.


# the generator

The generator tries to generate the hardest possible Minesweeper puzzles such as the following...

![small difficult sample puzzle](puzzle.jpg)

What I call Minesweeper *puzzles* have all clue numbers exposed before any clicks. There is one unique solution. Some would call these Tentaizu-like puzzles.

Unlike the best quality puzzles I've seen so far, found at [https://www.puzzle-minesweeper.com/](https://www.puzzle-minesweeper.com/), my code attempts to generate the hardest possible puzzles. This website's easy puzzles can be solved with purely one-number-at-a-time logic, and the rest of their puzzles can be solved with the addition of two-numbers-at-a-time logic (based on my 2024 tests). Unlike them, on top of a solver that knows this logic, I have a recursive (brute-force) algorithm that makes smart guesses before trying the less smart ones, allowing me to try to generate the hardest possible puzzles with the lowest runtime.

gen.cpp is the recommended generator. gen.py is kept as a readable Python reference version with an optional hybrid CP-SAT fallback. For gen.cpp, just enter the puzzle size, the desired difficulty, and an optional random seed.

Before 2026, I had a simple Python version, and no C++ version. In 2026, ChatGPT helped me!
* gen.cpp was written based on the simple Python version! It is almost 10 times faster in my tests.
* gen.py now has a hybrid solver that can eventually use Google's OR-Tools CP-SAT solver for hardest puzzles. CP-SAT provides a speedup for some very large and hard puzzles. Adding a CP-SAT solver to gen.cpp might also help for extremely large and hard puzzles, but probably less than it helps Python because gen.cpp already avoids most Python overhead. When gen.py switches to CP-SAT, any progress already made by the old solver is discarded instead of being encoded into the CP-SAT model.

Generator notes...
* The difficulty can be selected between [https://www.puzzle-minesweeper.com/](https://www.puzzle-minesweeper.com/)'s easy, their hard, and the actual hardest possible. The actual difficulty is displayed if the target difficulty was not reached. The above tiny puzzle is harder than the website's hard.
* Any rectangular puzzle size can be chosen.
* To prevent a human solver from having to count flags, the flag count varies. There is a 25% random chance of each square being a flag, and the code redoes all the flags if the result has a 5% deviation from 25%. These percentages can easily be changed.
* The random seed, the puzzle, and the SHA-256 hash of the solution are among the outputs.
* The code simply removes numbers from the puzzle in a predefined shuffled order and puts any back if removing them causes the puzzle to end up with more than 1 solution. All of the resulting puzzles have 1 unique solution.
* A 50×50 puzzle of the hardest difficulty should be generated in the range from a couple seconds to a couple days. For large puzzles like this, the exponential nature of the recursive search allows for much variability in runtime! In the code, there is a commented-out breadth-first search that attempted to speed things up (it slowed things down), but it might still be useful to split my hardest difficulty levels into two different levels. An advantage of the breadth-first search is that it is closer to how a human would solve a puzzle. Though I would be very cautious in trying to differentiate between types of guessing (brute forcing) before all other non-guessing logic has been implemented.
* The solver used by a generator that is trying to make a puzzle with one solution cannot use strategies that assume a unique solution. An example of this type of strategy is: if a 2 is surrounded by three unknowns that are connected to nothing besides the 2, those three unknowns can be cleared because, even if both of the flags were in those three locations, there is no way to uniquely determine how they should be arranged. My solver does not consider this type of logic, though perhaps it should when it tests the final puzzle for difficulty. Another interesting example is below. A dot (.) means an unknown, and the vertical line is the left edge of the board. If there is a single unique solution, the unknowns directly above the 3 and the 5 must be flags. This is a real example from a real Minesweeper puzzle.
    ```
  |...
  |...
  |35.
  |...
    ```


Next steps...
* If you care to make many huge (or just a couple of *very* huge) puzzles, there is probably a way to speed things up. Perhaps one could make a general non-brute-force solver that includes single-number and two-number logic as well as propagating to any number of numbers. I'm not convinced this will speed things up greatly, especially since Minesweeper is NP-complete. The solver will become linearly slower in order to prevent some exponential slowdown, though it could still become exponentially slower because information can be propagated across the entire puzzle. I probably will never pursue this because, when solving a Sudoku, you eventually should just make a guess or two instead of trying ever harder strategies.
* To get the truly hardest puzzles as fast as possible, already hard puzzles should be modified until even harder. Instead of just (1) replacing numbers with unknowns until numbers cannot be replaced anymore (what my code currently does), the code could then (2) replace one or more numbers with unknowns (giving the puzzle multiple solutions), then (3) randomly replace unknowns with numbers until a single solution exists (possibly giving a new solution), then (4) replace numbers with unknowns again keeping one solution, then (5) if the puzzle is more difficult (probably primarily based on number of guesses required), keep it (else revert), or add it to a pool of best versions. Steps 2-5 can be done on repeat. This procedure would not make the very small puzzle sizes much harder because, for them to be considered hard by my generator, they must require guessing, and, since the puzzle is a small size, the tricky part that requires guessing must be most of the puzzle. This might be a game changer!



# player and solver

Paste the puzzle size, the puzzle, and the SHA-256 hash into player.html to play the puzzle in your web browser!

To solve puzzles at [https://www.puzzle-minesweeper.com/](https://www.puzzle-minesweeper.com/) or from player.html, solve.py will take a screenshot, solve the puzzle, then can click the flags automatically. It uses basically the same solver as one of the solvers found in gen.py.


# thanks

I thank [Nacho-Meter-Stick](https://github.com/Nacho-Meter-Stick) for optimizing some lines of my code and inspiring me to try the depth=1 breadth-first search.

I also thank [https://www.puzzle-minesweeper.com/](https://www.puzzle-minesweeper.com/) for their great puzzles!

