#!/usr/bin/env python3.11

# Solve puzzles at
#   https://www.puzzle-minesweeper.com/
# Note that the beginner ones are very easy,
#   since you never have to look at more than 1 number at a time.
# For the non-beginner ones, you only have to look at 1 number at
#   a time and look at all pairs.

# Use 100% zoom in browser and OS.
# Else, the parts of the board jitter as browser window size is changed.
# When starting the code,
#   have mouse at the upper left of the board inside the blue border
# Then don't move your mouse for a few seconds while modules are imported
#   before the screenshot is taken.
# Another idea to implement is to, when the page is scrolled all the way to
#   the top, refresh the browser page using the Python code, then maybe
#   have some automated way of checking exactly when the page is refreshed.
#   Note that the "new puzzle" button is annoying because it refreshes the
#   page, so it resets your vertical scrolling, but you often have to scroll
#   down just to see it.
#     from time import sleep
#     pyautogui.press('f5')
#     sleep(1)  # 1 second should hopefully be enough!

# Use the default settings in the game...
#  - Cross
#  - Highlight errors
#  - Classic Mode
# For submitting best scores, you should strongly consider using...
#  - Auto submit

# With these setting, right click is to place flag.
# To win, all flags must be placed. Though with auto submit, you can also clear all boxes.
# Number of flags is not consistent given a puzzle type.
# Left click on box or on number clears, but why would I?

# macOS screenshots don't give the correct color.
# They have have some color profile applied to them,
#   so the colors need to be changed for macOS.
#   Maybe the hardware (monitor?) affects the exact filter?!
#   Different software all seems to apply this filter differently.
# Also, near the dock, the colors are changed,
#   so nothing can be near the dock! The dock can be moved!

# Each OS has different fonts for the numbers!!
# This can affect the color too because color is distorted near edges

# I also have some commented out parameters for making this solve my
#   player.html or player.py !
# Sadly, on my Ubuntu 22.04, player.py's puzzle is not displayed at the correct size.



import pyautogui    # pip-3.11 install pyautogui
import numpy as np
from itertools import combinations
from time import perf_counter



puzzSize = (20,20)    # rows, columns

doClicks = True


#outerBorderThickness = 1   # for player.html or player.py
outerBorderThickness = 3

pixelWidth = 31  # per number

pixelsInsideBlueBorder = 17





# Ubuntu
interestingPixel = (17,7)
colors = { (153, 153, 153): 0,
           (66, 134, 206): 1,     # not "central" color
           (0, 153, 1): 2,        # not "central" color
           (255, 0, 0): 3,
           (0, 0, 153): 4,
           (153, 0, 0): 5,
           (125, 180, 174): 6,    # not "central" color
           (0, 0, 0):   7,
           (153, 0, 153): 8,
           (238, 238, 238): -1,    # for player.html or player.py
           (204, 204, 204): -1 }


# macOS from GIMP output PNG
interestingPixel = (15,8)
colors = { (153, 153, 153): 0,
           (62, 4, 255): 1,
           (40, 153, 0): 2,
           (242, 32, 0): 3,
           (33, 2, 153): 4,
           (153, 16, 0): 5,
           (54, 153, 153): 6,
           (0, 0, 0): 7,
           (153, 0, 153): 8,
           (238, 238, 238): -1,    # for player.html or player.py
           (204, 204, 204): -1 }


# macOS
interestingPixel = (15,5)        # for player.py
interestingPixel = (15,8)
colors = { (153, 153, 153): 0,   # greys are the same
           (0, 60, 249): 1,
           (0, 151, 35): 2,
           (255, 0, 8): 3,
           (0, 32, 149): 4,
           (162, 0, 2): 5,
           (0, 154, 153): 6,
           (0, 0, 0): 7,            # greys are the same
           (158, 24, 149): 8,
           (238, 238, 238): -1,    # for player.html or player.py; greys are the same
           (204, 204, 204): -1 }    # greys are the same


# Windows
interestingPixel = (15,5)     # for player.py
interestingPixel = (15,7)     # for player.html
interestingPixel = (15,8)
colors = { (153, 153, 153): 0,
           (0, 0, 255): 1,
           (0, 153, 0): 2,
           (255, 0, 0): 3,
           (0, 0, 153): 4,
           (153, 0, 0): 5,
           (0,153,153): 6,
           (0, 0, 0):   7,
           (153,0,153): 8,
           (238, 238, 238): -1,    # for player.html or player.py
           (204, 204, 204): -1 }  # grey around a number (or in any box)




pyautogui.PAUSE = 0.0
np.set_printoptions(threshold=np.inf)



######## read in the puzzle into puzzle[i,j]

start = perf_counter()
coords = pyautogui.position()
sizeX = pixelWidth*puzzSize[1] + pixelsInsideBlueBorder
sizeY = pixelWidth*puzzSize[0] + pixelsInsideBlueBorder
image0 = pyautogui.screenshot(region=(coords.x, coords.y, sizeX, sizeY))  # PIL image
print("time =", perf_counter() - start, "s")
print("Screenshot complete")


#from PIL import Image         # pip-3.11 install pillow
#image0 = Image.open("week_feb12_2024_mac.png")


#print(image0.mode)
image0 = image0.convert("RGB")  # some OS's give an alpha value

#image0.show()
image = image0.load()   # NumPy array
x,y = 0,0  # *image* coordinates

# scan diagonally for grey border
# Note that the color 102 works even on macOS
while image[x,y][0] == 255:
  x += 1
  y += 1

# move vertically
while image[x,y-1][0] != 255:
  y -= 1

# move horizontally
while image[x-1,y][0] != 255:
  x -= 1

# move past grey border
x += outerBorderThickness
y += outerBorderThickness

# move to where the interesting discerning pixel is
x += interestingPixel[0]
y += interestingPixel[1]


puzzle = np.ones(puzzSize, dtype = np.int32)

# 0-8 is that number
# -1 is an unknown
for i in range(puzzSize[1]):
  for j in range(puzzSize[0]):
    puzzle[j,i] = colors[ image[x+pixelWidth*i,y+pixelWidth*j] ]

# x,y will now be for the screen (not image)
x += coords.x
y += coords.y




######## define various things from puzzle[]
# puzzle[] will not be used after this


start = perf_counter()

# locations of numbers in a 5 by 5 box surrounding each number
connections = np.empty(puzzSize, dtype=object)
# This list is not changed by the solver.

# locations of numbers in a 3 by 3 box surrounding each unknown
connections2 = np.empty(puzzSize, dtype=object)
# This list is not changed by the solver.

# locations of numbers in a 5 by 5 box surrounding each number
#   but the 4 corners are ignored
# Repeats of pairs are not listed (the other box will not connect back)
connectionsPairs = np.empty(puzzSize, dtype=object)

# number of flags needed to be placed.
#   int32 is faster than int8
flags = np.zeros(puzzSize, dtype=np.int32)

# unknowns surrounding each number
unknowns = np.empty(puzzSize, dtype=object)

# remaining numbers to be looped over
# For now, all numbers on board
remaining = []

for i in range(puzzSize[0]):
  for j in range(puzzSize[1]):

    # make connections2[]
    if puzzle[i,j] == -1:

        c = []
        for i2 in range(max(0,i-1), min(puzzSize[0],i+2)):
          for j2 in range(max(0,j-1), min(puzzSize[1],j+2)):
            if puzzle[i2,j2] != -1:
              c.append((i2,j2))
        connections2[i,j] = tuple(c)

        continue

    # make remaining[] and flags[]
    remaining.append((i,j))
    flags[i,j] = puzzle[i,j]

    # find unknowns surrounding each number
    u = set()
    for i2 in range(max(0,i-1), min(puzzSize[0],i+2)):
      for j2 in range(max(0,j-1), min(puzzSize[1],j+2)):
        if puzzle[i2,j2] == -1:
          u.add((i2,j2))
    unknowns[i,j] = u

    # find connections and connectionsPairs
    c = []
    c1 = []
    for i2 in range(i, min(puzzSize[0],i+3)):
      for j2 in range(max(0,j-2), min(puzzSize[1],j+3)):
        if puzzle[i2,j2] != -1:
          c.append((i2,j2))
          if (i2-i)!=2 or abs(j-j2)!=2:  # remove 4 corners
            if i2>i or (i2==i and j2>j): # list each pair only once and do not list itself
              c1.append((i2,j2))
    connections[i][j] = tuple(c)

    #connectionsPairs[i][j] = tuple(c1)
    connectionsPairs[i][j] = set(c1)
    # I use sets because of an (incomplete) attempt to remove elements of these,
    #   though it may be fastest to never try to alter connectionsPairs at all!

#print(connections)
#print(connections2)
#print(connectionsPairs)
#print(unknowns)



# make a lookup table for n-choose-r
nCr = np.array([[1,0,0,0,0,0,0,0,0,],[1,1,0,0,0,0,0,0,0,],[1,2,1,0,0,0,0,0,0,],[1,3,3,1,0,0,0,0,0,],[1,4,6,4,1,0,0,0,0,],[1,5,10,10,5,1,0,0,0,],[1,6,15,20,15,6,1,0,0,],[1,7,21,35,35,21,7,1,0,],[1,8,28,56,70,56,28,8,1]], dtype = np.uint32)



# to store where the flags are placed
sol = np.zeros(puzzSize, dtype=np.int32)

print("time =", perf_counter() - start, "s")



######## make a solve function that does not do guessing
# It will look at a single number at a time, or two numbers at a time.


# returns -1 if there is proven to be 0 solutions
def solve(remaining, flags, unknowns, sol, connectionsPairs):

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
          for c in connections2[j]:
            unknowns[c].remove(j)
            if len(unknowns[c]) < flags[c]:
              return -1

      # check for solved numbers (look at a single number at a time)
      elif flags[current]==len(unknowns[current]):
        change = True
        # update remaining, flags, sol, and unknowns
        remaining.pop(i)
        for j in unknowns[current].copy():
          sol[j] = 1
          for c in connections2[j]:
            if flags[c]:
              flags[c] -= 1
              unknowns[c].remove(j)
            else:
              return -1

      # look at pairs at a time
      elif len(unknowns[current]) > 1:  # at least 2 in intersection to be useful
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
                sol[j] = 1
                for c in connections2[j]:
                  if flags[c]:
                    flags[c] -= 1
                    unknowns[c].remove(j)
                  else:
                    return -1
            if L1:   # clear the unknowns in set1
              changeTemp = True
              for j in set1:
                for c in connections2[j]:
                  unknowns[c].remove(j)
                  if len(unknowns[c]) < flags[c]:
                    return -1
          elif flags[k] == maxFlagsInIntersection and flags[current] == L1 + maxFlagsInIntersection:
            if L1:   # add flags to the unknowns in set1
              changeTemp = True
              for j in set1:   # loop over each flag that is placed
                sol[j] = 1
                for c in connections2[j]:
                  if flags[c]:
                    flags[c] -= 1
                    unknowns[c].remove(j)
                  else:
                    return -1
            if L2:   # clear the unknowns in set2
              changeTemp = True
              for j in set2:
                for c in connections2[j]:
                  unknowns[c].remove(j)
                  if len(unknowns[c]) < flags[c]:
                    return -1
          if changeTemp:
            connectionsPairs[current].remove(k)
            change = True

  return 0





######## do the recursion!
# Note that https://www.puzzle-minesweeper.com/ will never
#   require guessing or recursion.




def recursive_func(remaining, flags, unknowns, sol, connectionsPairs):

  # solve whatever we can solve without guessing. This greatly speeds things up!
  if solve(remaining, flags, unknowns, sol, connectionsPairs) == -1:
    return

  if not remaining:   # if solved
    t = perf_counter() - start

    if doClicks:
      for i in range(puzzSize[1]):
        for j in range(puzzSize[0]):
          if sol[j,i]:
            pyautogui.click(button='right',x= x+pixelWidth*i,y= y+pixelWidth*j)

    print(sol)
    print("time =", t, "s")
    exit()   # don't look for multiple solutions


  r3 = sorted(remaining, key=lambda x: nCr[len(unknowns[x]), flags[x]], reverse=True)   # this sorting by number of combinations GREATLY improves the speed!
  current = r3.pop()

  # Note that combinations( ,0) puts i=() through the loop.
  # Note that combinations([1,2,3],4) has no iterations.
  for i in combinations(unknowns[current], flags[current]):

    # shallow copy (greatly speeds things up compared to deep copy!)
    u2 = unknowns.copy()
    for c in remaining:
      u2[c] = unknowns[c].copy()

    # update u2
    for j in unknowns[current]: 
      for c in connections2[j]:
        u2[c].remove(j)

    c2 = np.copy(connectionsPairs)
    for c in remaining:
      c2[c] = connectionsPairs[c].copy()

    r2 = r3.copy()
    f2 = np.copy(flags)
    s2 = np.copy(sol)

    #def update_flags():
    # The following is faster than making it a function
    stop = False
    for j in i:   # loop over each flag that is placed
      s2[j] = 1
      #if stop:
      #  break
      for c in connections2[j]:
          if f2[c]:
            f2[c] -= 1
          else:
            stop = True
            break
    if stop:
      continue

    # Check if there are fewer unknowns than flags to place
    # Is the following necessary? does it speed things up?
    # This must be done after flags[] is updated
    stop = False
    for j in unknowns[current]:
      #if stop:
      #  break   
      for c in connections2[j]:
        if len(u2[c]) < f2[c]:  # unnecessarily checks for "flag unknowns" too
           stop = True
           break
    if stop:
      continue

    recursive_func(r2, f2, u2, s2, c2)


print("Starting search")
start = perf_counter()
recursive_func(remaining, flags, unknowns, sol, connectionsPairs)




