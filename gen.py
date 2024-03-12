#!/usr/bin/env python3.11
#
# Generate minesweeper puzzles and solution SHA-256 hash similar to those at
#   https://www.puzzle-minesweeper.com/
# Feel free to change the distribution of number of flags
#   in the code.


import numpy as np
from itertools import combinations
from time import perf_counter
from hashlib import sha256


puzzSize = (50,50)
# If you make this too small, the target range for flag count may not be
#   possible, and the code will try forever.

difficulty = 2   # 0, 1, or 2
# 0 is easy. Only one-number-at-a-time logic is needed.
# 1 is medium. Two-numbers-at-a-time logic is also needed.
# 2 is hard. Extra logic is needed (done via guessing in this code)
#
# If difficulty > 0, this is the ALLOWED difficulty. There is no guarantee that it will
#   be this hard, but it won't be harder.
# The code will check if the target difficulty was obtained.



seed = np.random.randint(0, high=10000000)  # feel free to increase high
seed = 8117835     # you can manually set seed here

rng = np.random.default_rng(seed=seed)
# The only things that use this seed are...
#    rng.integers(low=0, high=4, size=puzzSize)
#    rng.shuffle(remainingToTry)
# Note that changing the allowed range for flagNum can sometimes change
#   the results given the same seed.



np.set_printoptions(threshold=np.inf)


# make a lookup table that is nCr(n,r)
nCr = np.array([[1,0,0,0,0,0,0,0,0,],[1,1,0,0,0,0,0,0,0,],[1,2,1,0,0,0,0,0,0,],[1,3,3,1,0,0,0,0,0,],[1,4,6,4,1,0,0,0,0,],[1,5,10,10,5,1,0,0,0,],[1,6,15,20,15,6,1,0,0,],[1,7,21,35,35,21,7,1,0,],[1,8,28,56,70,56,28,8,1]], dtype = np.uint32)



######## make a solve function that does not do guessing
# It will look at a single number at a time, or two numbers at a time.


# Returns -1 if there is proven to be 0 solutions.
# Modifies remaining, flags, unknowns, and connectionsPairs.
def solve(remaining, flags, unknowns, connectionsPairs, connections):

  change = True

  while change:

    change = False

    for i in range(len(remaining)-1, -1, -1):
      current = remaining[i]

      # check for solved numbers (look at a single number at a time)
      if not flags[current]:
        change = True
        # update remaining and unknowns
        remaining.pop(i)
        for j in unknowns[current].copy():
          for c in connections[j]:
            unknowns[c].remove(j)
            if len(unknowns[c]) < flags[c]:
              return -1

      # check for solved numbers (look at a single number at a time)
      elif flags[current]==len(unknowns[current]):
        change = True
        # update remaining, flags, sol, and unknowns
        remaining.pop(i)
        for j in unknowns[current].copy():
          for c in connections[j]:
            if flags[c]:
              flags[c] -= 1
              unknowns[c].remove(j)
            else:
              return -1

      # look at pairs at a time
      elif difficulty and len(unknowns[current]) > 1:  # at least 2 in intersection to be useful
        for k in connectionsPairs[current].copy():

          intersect = unknowns[current].intersection(unknowns[k])
          L3 = len(intersect)
          if L3 < 2:
            connectionsPairs[current].remove(k)
            continue
          set1 = unknowns[current].difference(intersect)
          set2 = unknowns[k].difference(intersect)
          L1 = len(set1)
          L2 = len(set2)
          if not (L1 or L2):
            connectionsPairs[current].remove(k)
            continue
          maxFlagsInIntersection = min(flags[current], flags[k], L3)

          changeTemp = False
          if flags[current] == maxFlagsInIntersection and flags[k] == L2 + maxFlagsInIntersection:
            if L2:   # add flags to the unknowns in set2
              changeTemp = True
              for j in set2:   # loop over each flag that is placed
                for c in connections[j]:
                  if flags[c]:
                    flags[c] -= 1
                    unknowns[c].remove(j)
                  else:
                    return -1
            if L1:   # clear the unknowns in set1
              changeTemp = True
              for j in set1:
                for c in connections[j]:
                  unknowns[c].remove(j)
                  if len(unknowns[c]) < flags[c]:
                    return -1
          elif flags[k] == maxFlagsInIntersection and flags[current] == L1 + maxFlagsInIntersection:
            if L1:   # add flags to the unknowns in set1
              changeTemp = True
              for j in set1:   # loop over each flag that is placed
                for c in connections[j]:
                  if flags[c]:
                    flags[c] -= 1
                    unknowns[c].remove(j)
                  else:
                    return -1
            if L2:   # clear the unknowns in set2
              changeTemp = True
              for j in set2:
                for c in connections[j]:
                  unknowns[c].remove(j)
                  if len(unknowns[c]) < flags[c]:
                    return -1

          if changeTemp:
            connectionsPairs[current].remove(k)
            change = True


  return 0





######## make a solve function that does a breadth-first search at a depth of 1
# For each in remaining, try a flag or a clear. If there is a contradiction, the other must be correct.
#
# I do not use this. It slows down the solver.
# If I ever did use it, I would be sure to add the depth parameters so that they still print when difficulty=2,
#   where depth would be the depth of the RECURSIVE solver.


# Returns -1 if there is proven to be 0 solutions.
# Modifies remaining, flags, unknowns, and connectionsPairs.
def breadth_first_search(remaining, flags, unknowns, connectionsPairs, connections):
  global solCount

  change = True

  while change:   # keep going as long as progress is being made!

    change = False

    # instead I should create and update a list of unknowns instead??
    for i in range(puzzSize[0]):
     for j in range(puzzSize[1]):

      current = (i,j)

      # make sure that this is an unknown
      if not connections[current]:
        continue
      if current not in unknowns[ connections[current][0] ]:
        continue


      ## try adding a flag

      # shallow copy as much as possible (greatly speeds things up compared to deep copy!)
      u2 = unknowns.copy()
      for c in remaining:
        u2[c] = unknowns[c].copy()
      c2 = np.copy(connectionsPairs)
      for c in remaining:
        c2[c] = connectionsPairs[c].copy()
      r2 = remaining.copy()
      f2 = np.copy(flags)

      # add flag
      contradiction = False
      for c in connections[current]:
        if f2[c]:
          f2[c] -= 1
          u2[c].remove(current)
        else:
          contradiction = True
          break

      # call solve()
      if not contradiction:
        if solve(r2, f2, u2, c2, connections) == -1:
          contradiction = True

      if contradiction:  # this square is a clear!

        # clear the location
        for c in connections[current]:
          unknowns[c].remove(current)
          if len(unknowns[c]) < flags[c]:
            return -1

        if solve(remaining, flags, unknowns, connectionsPairs, connections) == -1:
          return -1

        if len(remaining) == 0:
          return 0

        change = True
        #print(",")
        continue


      ## try adding a clear

      # shallow copy as much as possible (greatly speeds things up compared to deep copy!)
      u3 = unknowns.copy()
      for c in remaining:
        u3[c] = unknowns[c].copy()
      c3 = np.copy(connectionsPairs)
      for c in remaining:
        c3[c] = connectionsPairs[c].copy()
      r3 = remaining.copy()
      f3 = np.copy(flags)

      # update u3
      contradiction = False
      for c in connections[current]:
        u3[c].remove(current)
        if len(u3[c]) < f3[c]:
          contradiction = True
          break

      # call solve()
      if not contradiction:
        if solve(r3, f3, u3, c3, connections) == -1:
          contradiction = True

      if contradiction:  # this square is a flag!

        unknowns = u2
        connectionsPairs = c2
        remaining = r2
        flags = f2

        if len(remaining) == 0:
          return 0

        change = True
        #print(".")
        continue


      ## handle if either or both situations were solved

      if not len(r3) and not len(r2):  # handle both being solved
        solCount += 2
        return 0

      elif not len(r3):          # r3 is solved
        solCount += 1
        if solCount > 1:
          return 0

        unknowns = u2
        connectionsPairs = c2
        remaining = r2
        flags = f2

        change = True

      elif not len(r2):          # r2 is solved
        solCount += 1
        if solCount > 1:
          return 0

        unknowns = u3
        connectionsPairs = c3
        remaining = r3
        flags = f3

        change = True





  return 0



######## the recursive solver function



# solCount should be 0 before calling this function, then...
#  0 if the puzzle was not solved (if difficulty=2, this means the puzzle actually has 0 solutions, so this never occurs for difficulty=2)
#  1 if there is 1 unique solution
#  2+ if there are 2+ or more solutions (only occurs if difficulty=2)

finalBranches = 0  # value does not matter yet, but we must define the global variable
# This variable is useful for analyzing a difficulty=2 puzzle that is known to have 1 unique solution.

def recursive_func(remaining, flags, unknowns, connectionsPairs, connections, printDepth=False, depth=0):
  global solCount, finalBranches

  # solve whatever we can solve without guessing. This greatly speeds things up!
  if solve(remaining, flags, unknowns, connectionsPairs, connections) == -1:
    finalBranches += 1
    return

  if not remaining:   # if solved
    if printDepth:
      print("depth =", depth)
    solCount += 1
    finalBranches += 1
    return

  if difficulty < 2:
    return


  # this does not speed things up, though could possibly be used to define another difficulty level
  '''
  if not depth:
    if breadth_first_search(remaining, flags, unknowns, connectionsPairs, connections) == -1:
      finalBranches += 1
      return

    if not remaining:   # if solved
      if printDepth:
        print("depth =", depth)
      solCount += 1
      finalBranches += 1
      return
  '''


  r3 = sorted(remaining, key=lambda x: nCr[len(unknowns[x]), flags[x]], reverse=True)   # this sorting by number of combinations GREATLY improves the speed!
  current = r3.pop()

  if not nCr[len(unknowns[current]), flags[current]]:
    print("this happens!")
    finalBranches += 1
    return   # unnecessary

  # Note that combinations( ,0) puts i=() through the loop.
  # Note that combinations([1,2,3],4) has no iterations.
  for i in combinations(unknowns[current], flags[current]):

    if solCount>1:    # stop counting after 2
      break

    # shallow copy (greatly speeds things up compared to deep copy!)
    u2 = unknowns.copy()
    for c in remaining:
      u2[c] = unknowns[c].copy()

    # update u2
    for j in unknowns[current]: 
      for c in connections[j]:
        u2[c].remove(j)

    c2 = np.copy(connectionsPairs)
    for c in remaining:
      c2[c] = connectionsPairs[c].copy()

    r2 = r3.copy()
    f2 = np.copy(flags)

    #def update_flags():
    # The following is faster than making it a function that can use return to cut out of it
    stop = False
    for j in i:   # loop over each flag that is placed
      #if stop:
      #  break
      for c in connections[j]:
          if f2[c]:
            f2[c] -= 1
          else:
            stop = True
            break
    if stop:
      finalBranches += 1
      continue

    # Check if there are fewer unknowns than flags to place
    # Is the following necessary? does it speed things up?
    # This must be done after flags[] is updated
    stop = False
    for j in unknowns[current]:
      #if stop:
      #  break   
      for c in connections[j]:
        if len(u2[c]) < f2[c]:  # unnecessarily checks for "flag unknowns" too
           stop = True
           break
    if stop:
      finalBranches += 1
      continue

    recursive_func(r2, f2, u2, c2, connections, printDepth, depth+1)





########  main

#while True:
#for _ in range(10):
if True:

    start = perf_counter()



    # -1 is unknowns
    # 0-8 are those numbers
    puzzle = np.zeros(puzzSize, dtype = np.int32)

    # Add flags to, on average, 25% of the squares
    # Enforcing it to be between 20% and 30% of the squares
    average = puzzSize[0]*puzzSize[1]/4
    flagNum = 0
    while abs(flagNum - average)/average > .05:   # feel free to change the .05
      isFlag =  rng.integers(low=0, high=4, size=puzzSize) == 0
      flagNum = np.sum(isFlag)
    puzzle[isFlag] = -1

    # Change all 0s to numbers and make remainingToTry[]
    remainingToTry = []
    for i in range(puzzSize[0]):
      for j in range(puzzSize[1]):
        if isFlag[i,j]:
          continue

        for i2 in range(max(0,i-1), min(puzzSize[0],i+2)):
          for j2 in range(max(0,j-1), min(puzzSize[1],j+2)):
            if isFlag[i2,j2]:
              puzzle[i,j] += 1

        remainingToTry.append((i,j))

        
    rng.shuffle(remainingToTry)



    ## make the base ("0") data structures to be later copied to become the non-0 ones

    # locations of numbers in a 3 by 3 box surrounding each unknown
    connections0 = np.empty(puzzSize, dtype=object)
    # The solver does not change this variable, though the generator does.

    # locations of numbers in a 5 by 5 box surrounding each number
    #   but the 4 corners are ignored
    # Repeats of pairs are not listed (the other box will not connect back)
    connectionsPairs0 = np.empty(puzzSize, dtype=object)

    # number of flags needed to be placed.
    #   int32 is faster than int8
    flags0 = np.zeros(puzzSize, dtype=np.int32)

    # unknowns surrounding each number
    unknowns0 = np.empty(puzzSize, dtype=object)

    # remaining numbers to be looped over
    # For now, all numbers on board
    remaining0 = []

    for i in range(puzzSize[0]):
        for j in range(puzzSize[1]):

            # make connections0[]
            if puzzle[i,j] == -1:   # fun fact: right now, all -1 are flags

                c = []
                for i2 in range(max(0,i-1), min(puzzSize[0],i+2)):
                  for j2 in range(max(0,j-1), min(puzzSize[1],j+2)):
                    if i2==i and j2==j:
                      continue
                    if puzzle[i2,j2] != -1:
                      c.append((i2,j2))
                connections0[i,j] = c[:]

                continue

            # make remaining0[] and flags0[]
            remaining0.append((i,j))
            flags0[i,j] = puzzle[i,j]

            # find unknowns0 surrounding each number
            u = set()
            for i2 in range(max(0,i-1), min(puzzSize[0],i+2)):
              for j2 in range(max(0,j-1), min(puzzSize[1],j+2)):
                if puzzle[i2,j2] == -1:
                  u.add((i2,j2))
            unknowns0[i,j] = u

            # find connectionsPairs0
            c = []
            for i2 in range(i, min(puzzSize[0],i+3)):
              for j2 in range(max(0,j-2), min(puzzSize[1],j+3)):
                if puzzle[i2,j2] != -1:
                  if (i2-i)!=2 or abs(j-j2)!=2:  # remove 4 corners
                    if i2>i or (i2==i and j2>j): # list each pair only once and do not list itself
                      c.append((i2,j2))
            connectionsPairs0[i,j] = set(c)
            # I use sets because of an (incomplete) attempt to remove elements of these,
            #   though it may be fastest to never try to alter connectionsPairs at all!




    # For each in remainingToTry[], try to replace with a -1.
    # If the puzzle still has 1 and only 1 solution, keep it a -1,
    #   else put it back.
    for _ in range(len(remainingToTry)):
        k = remainingToTry.pop()
        i,j = k


        ## make sure that all unknowns will have a number around them!

        # check surrounding unknowns
        stop = False
        for c in unknowns0[k]:
          if len(connections0[c]) < 2:
            stop = True
            break
        if stop:
          continue

        # check the new unknown at k to make sure a number surrounds it
        stop = True
        for i2 in range(max(0,i-1), min(puzzSize[0],i+2)):
          for j2 in range(max(0,j-1), min(puzzSize[1],j+2)):
            if i2==i and j2==j:
              continue
            if puzzle[i2,j2] != -1:
              stop = False
              break
        if stop:
          continue



        ## update the data structures
        ## flags[k], unknowns[k], and connectionsPairs[k] do not need updating
        ## Deep copy any part that could get changed by the solver (puzzle[] and connections[] are not changed).
        ## Deep copy any part that will be reverted if removing k gives multiple solutions.

        # no copy at all!
        num = puzzle[k]
        puzzle[k] = -1

        # complete deep copy
        remaining = remaining0.copy()
        remaining.remove(k)

        # complete deep copy
        flags = np.copy(flags0)

        # only deep copy the changes
        connections = np.copy(connections0)  # shallow copy
        connections[k] = []   # deep copy here
        for i2 in range(max(0,i-1), min(puzzSize[0],i+2)):
          for j2 in range(max(0,j-1), min(puzzSize[1],j+2)):
            if i2==i and j2==j:
              continue
            if puzzle[i2,j2] == -1:
              # would a list comprehension be faster?
              connections[i2,j2] = connections0[i2,j2].copy()   # deep copy here
              connections[i2,j2].remove(k)
            else:
              connections[k].append((i2,j2))

        # essentially a complete deep copy because only unknowns[] at remaining matter
        unknowns = unknowns0.copy()
        for c in remaining:
          unknowns[c] = unknowns0[c].copy()
        for c in connections[k]:  # connections[k] must first be updated
          unknowns[c].add(k)

        # essentially a complete deep copy because only connectionsPairs[] at remaining matter
        connectionsPairs = np.copy(connectionsPairs0)
        for c in remaining:
          connectionsPairs[c] = connectionsPairs0[c].copy()
        for i2 in range(max(0,i-2), i+1):
          for j2 in range(max(0,j-2), min(puzzSize[1],j+3)):
            if puzzle[i2,j2] != -1:
              if (i-i2)!=2 or abs(j-j2)!=2:  # remove 4 corners
                if i2<i or (i2==i and j2<j):
                  connectionsPairs[i2,j2].remove(k)




        ## do it!

        solCount = 0
        recursive_func(remaining, flags, unknowns, connectionsPairs, connections)


        '''
        # print progress info useful when making huge puzzles that take a long time to make
        # If all unknowns did not have a number around them, neither will print
        if solCount != 1:
          print("puzzle made no harder.", len(remainingToTry), "left")
        else:
          print("puzzle made harder!", len(remainingToTry), "left")
        '''


        ## update things

        if solCount != 1:
          puzzle[k] = num   #put back number
        else:

          ## update "0" data structures

          remaining0.remove(k)

          connections0[k] = connections[k]
          for i2 in range(max(0,i-1), min(puzzSize[0],i+2)):
            for j2 in range(max(0,j-1), min(puzzSize[1],j+2)):
              if i2==i and j2==j:
                continue
              if puzzle[i2,j2] == -1:
                connections0[i2,j2] = connections[i2,j2]

          for c in connections0[k]:
            unknowns0[c].add(k)

          for i2 in range(max(0,i-2), i+1):
            for j2 in range(max(0,j-2), min(puzzSize[1],j+3)):
              if puzzle[i2,j2] != -1:
                if (i-i2)!=2 or abs(j-j2)!=2:  # remove 4 corners
                  if i2<i or (i2==i and j2<j):
                    connectionsPairs0[i2,j2].remove(k)





    def arrayToString(ar):  # for 2D Numpy arrays
      s = ''
      for i in range(ar.shape[0]):
        for j in range(ar.shape[1]):
          s += str(ar[i,j])
      return s



    print()
    print(puzzSize)

    print(puzzle)
    puzzle[puzzle==-1] = 9
    print(arrayToString(puzzle))

    print()
    isFlag = np.uint32(isFlag)
    print(isFlag)
    strFlat = arrayToString(isFlag)
    #print(strFlat)
    print("hash =", sha256(strFlat.encode()).hexdigest() )
    print()



    if difficulty:   # have it run again on a smaller difficulty

      # make copies
      remaining = remaining0.copy()
      flags = flags0.copy()
      unknowns = unknowns0.copy()
      for c in remaining:
        unknowns[c] = unknowns0[c].copy()
      connectionsPairs = np.copy(connectionsPairs0)
      for c in remaining:
        connectionsPairs[c] = connectionsPairs0[c].copy()

      if difficulty == 1:

        difficulty = 0
        solCount = 0
        recursive_func(remaining, flags, unknowns, connectionsPairs, connections0)

        if not len(remaining):  # if solved
          print("Target difficulty not reached!")
          print("difficulty = 0")

        difficulty = 1

      elif difficulty == 2:

        difficulty = 1
        solCount = 0
        recursive_func(remaining, flags, unknowns, connectionsPairs, connections0)

        if not len(remaining):  # if solved

          difficulty = 0
          solCount = 0
          recursive_func(remaining0, flags0, unknowns0, connectionsPairs0, connections0)

          print("Target difficulty not reached!")
          if not len(remaining0):  # if solved
            print("difficulty = 0")
          else:
            print("difficulty = 1")

          difficulty = 2

        else:

          # Print depth and finalBranches
          # These are mostly meaningless but kinda interesting data.
          # They are meaningless because they depend on the exact order that
          #   numbers are traversed in the recursive solver. In the current state
          #   of the code, it is also unclear if the data are due
          #   to a bunch of easy situations or one very hard one or whatever.
          # Note that depth may go deeper for branches that did not find the solution.
          difficulty = 2
          solCount = 0
          finalBranches = 0
          recursive_func(remaining0, flags0, unknowns0, connectionsPairs0, connections0, True, 0)
          print("finalBranches =", finalBranches)

          #exit()  # when I'm looking for the first difficulty=2 of a small puzzSize


    print("seed =", seed)
    print(perf_counter() - start, "s")
    print()
