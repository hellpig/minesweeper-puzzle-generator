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
#   the top, refresh the browser page using the Python code.
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

# I also have some commented out parameters for making this solve my player.py !
# Sadly, on my Ubuntu 22.04, player.py's puzzle is not displayed at the correct size.



import pyautogui    # pip-3.11 install pyautogui
import numpy as np
import scipy.optimize as optimize
from time import perf_counter



puzzSize = (20,20)    # rows, columns

doClicks = True


outerBorderThickness = 1   # for player.py
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
           (238, 238, 238): -1,    # for player.py
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
           (238, 238, 238): -1,    # for player.py
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
           (238, 238, 238): -1,    # for player.py; greys are the same
           (204, 204, 204): -1 }    # greys are the same


# Windows
interestingPixel = (15,5)     # for player.py
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
           (238, 238, 238): -1,    # for player.py
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




######## solve function

def solve_puzzle_w_matrix(puzzle):

    numbers_coords = np.argwhere(puzzle != -1)
    b = np.array([puzzle[tuple(num_coord)] for num_coord in numbers_coords], dtype = np.int8)

    # use hashmap to make puzzle-to-matrix conversion O(n)
    mp = {tuple(unk): i for i, unk in enumerate(np.argwhere(puzzle == -1))}
    len_u = len(mp)
    A = np.zeros((len(numbers_coords), len_u), dtype = np.int8)
    for i, n in enumerate(numbers_coords):
        # All 8 adjacent coords (if coord out of bounds, "(coord) in mp" will return false)
        if (n[0]-1, n[1]-1) in mp: A[i, mp[(n[0]-1, n[1]-1)]] = 1
        if (n[0]-1, n[1]) in mp:   A[i, mp[(n[0]-1, n[1])]] = 1
        if (n[0]-1, n[1]+1) in mp: A[i, mp[(n[0]-1, n[1]+1)]] = 1
        if (n[0], n[1]-1) in mp:   A[i, mp[(n[0], n[1]-1)]] = 1
        if (n[0], n[1]+1) in mp:   A[i, mp[(n[0], n[1]+1)]] = 1
        if (n[0]+1, n[1]-1) in mp: A[i, mp[(n[0]+1, n[1]-1)]] = 1
        if (n[0]+1, n[1]) in mp:   A[i, mp[(n[0]+1, n[1])]] = 1
        if (n[0]+1, n[1]+1) in mp: A[i, mp[(n[0]+1, n[1]+1)]] = 1

    # If multiple solutions exist, one of them is returned
    ret =  optimize.milp(c=np.zeros(len_u),                # np.ones() works too
                         integrality=np.full(len_u, 1),    # 1 or 3 works
                         bounds=optimize.Bounds(lb=np.zeros(len_u), ub=np.ones(len_u)), 
                         constraints=optimize.LinearConstraint(A, lb=b, ub=b))

    # if no solutions are found
    if not ret.success:
      return

    sol = ret.x

    # Put back into shape of the puzzle
    sol2 = np.zeros(puzzle.shape, dtype=np.int32)
    for unk in mp:
      if sol[ mp[unk] ]:
        sol2[unk] = 1

    return sol2



######## do it!

start = perf_counter()
sol = solve_puzzle_w_matrix(puzzle)
t = perf_counter() - start

print(sol)
print(f"  solve time = {t} s")

if sol and doClicks:
  for i in range(puzzSize[1]):
    for j in range(puzzSize[0]):
      if sol[j,i]:
        pyautogui.click(button='right',x= x+pixelWidth*i,y= y+pixelWidth*j)


