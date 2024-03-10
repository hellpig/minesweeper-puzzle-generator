#!/usr/bin/env python3.11


'''
Based on code from
   https://jakevdp.github.io/blog/2012/12/06/minesweeper-in-matplotlib/
I converted it to be from the classic Minesweeper game to the Minesweeper-puzzle format.

To align with https://www.puzzle-minesweeper.com/,
  the puzzle is solved when all FLAGS (⚑) are correctly placed.
When solved, background color will turn yellow,
  then no further clicks are allowed.
Solution hash is checked whenever a flag is placed.
I currently just have the user enter the SHA-256 hashed solution,
  but I could have instead put in a solver that solves the puzzle
  so that the solution isn't required.

I don't like how his NumPy arrays are indexed column then row
  and how the row counting starts from the bottom, but I never changed it
  because it is consistent with Matplotlib's data coordinates.

I fixed the broken .remove() from his code.

I changed most colors and dimensions to match
  https://www.puzzle-minesweeper.com/
except for covered_color, which I kept to make the board look nice
  (now 238 instead of 204)

Compared to him, I value simplicity.
I use fewer imported modules, and I do not use Object Oriented Programming.

His license is below.
'''


'''
Matplotlib Minesweeper
----------------------
A simple Minesweeper implementation in matplotlib.

Author: Jake Vanderplas <vanderplas@astro.washington.edu>, Dec. 2012
License: BSD
'''


import numpy as np
import matplotlib.pyplot as plt
from matplotlib.patches import RegularPolygon
from hashlib import sha256




size = (5, 5)    # rows, columns
puzzle = "9939119999193999239199919"
solution = "2e32c8bc44394e9799a6c9dfd512c186212b99b49c78d9b3c8245a464bc082bd"   #SHA-256

size = (4, 4)    # rows, columns
puzzle = "9199192929929229"
solution = "4da3806d9a9b6ff8612b05193be143f434443a13c3c5d0792f4ccebfc5c5317c"   #SHA-256

size = (3, 4)    # rows, columns
puzzle = "919919929119"
solution = "14a630447c01ddca29842cb69428735dfd98fe77b77ef99d3c7f7ecd1dd63cbc"   #SHA-256



size = (50, 50)    # rows, columns
puzzle = "9999999991993393921190992999019993933999909999993911139929939994999999192999499999499999442919212299911199992995959994999199299949499399399999999999999999299319939394991999192299939299199929993391999399999499999999991999339993949909199912199394999199929293992399999999959999992999999919999999949993299291990999909393999999399919999099994349992995939999191993939999939199999919929939999999999139992393099999299990992999090992999909992936996992995999999199999939992999992919919919999999999992999999999299991939299995929929921999990929922943992995649129231991393929999993399999199993991999991999399994999991999919999999999299949199399992199192929994999999999991990990193999993999399993993299299999993449979939999099999229992999594929299999999249919199999994994939929999329339919999999392999119993929939299399499929939993299992199939999111099449932999921019399999939999399119299949919929991999922999929999992993499999929999019999991299399991999991919999923992999991990909909999299999294992999922999019329994999934991999991994999299999999299991993999999199399199991929199992999991990193990919999993999599199399992219999929999992399999199991991919293999991999919399999399929239999199194932995921999939394933993919949919499092949991929999999999299999949399994999193999939999929999199995929949939392249999999999399999939999329949910999991929999999199594991929199399112993339999999199192939399999199999999999191999929999299999112999999999999932099092999999909912992923999939992991919902999949999999199492901999299292999999993999199291999392992999999999993929999399943993129494999499990991919919091999194999929999932999919929999399929999199019999299299949999915999299219999439991999299999999923999199909994919999919299929449919999299923599929999299990999999109994999929993994999909299994995999909990999299219993993991999994999909999199993999909992999129912999999991993399999993999901919199439939929929999999933293399999992919991199911099929219999929299993339392939929999199919999199999919919999999999991292999995994943199091999993499229229919959449593299999299999999929999994493999999999299939933999992999999492492399339499999399954399939991994399299392993299499929999495993393299991993999993999299909999299999999394999399221299959999469912992991999199993595951999939999999993939399999999992919999299239499949999211999110992999999229995993999991999199999449933191919909909923991099993993992992299193990993999929919999999993999919324994390992199299999992999929909919099091929939999999999991929999922992099210119"
solution = "40109122f5db90d59f281c87d09c0196bbca9c6d24c83835f8a344c068661b0c"   #SHA-256


size = (30, 30)    # rows, columns
puzzle = "929999191999191991999219291292939399999199999099993999992999299109099902939999092992499919929290919929999939999992991999999999999299949993939399999909924994990399993995999929929999993499999999429919959932999931999399211999200999949999229999299919999921999992999292999299999909999999999199939299999991949229094990909199999199490992939999092999999995990999999929239999992991199999999919911999999299291999919909391199999109911194992949919093949999999999919939999992999999999991909999291949999919099994999299099930999999799192939439959399993929929949992929999395999999119949092999911999459999990993992999999599992229949923299929999999939949399923992299199939999990199349999992299999992994993329949999499229999991999993439999299995999999939299299949999549294929942999993919929299949999999999999199392932999329999999992299099090913999911999990192199929939199999999299993999199993991999999129493192999939929"
solution = "2542ee1f47d9c4dbf5aa0f3cc4c5c42c211b31a699ef133305c10bb56a362764"   #SHA-256



#size = (10, 20)
#puzzle = "01234567899099099299299333399219909199930999939949999999939999929949991992909199999999929299299999499099299993999399992199499019499999109929299399919994999999999999919199999399292191129990939392199991"  # for seeing how 0-8 look



def MineSweeper(size, puzzle, solution):


    covered_color = '#eeeeee'
    uncovered_color = '#cccccc'
    edge_color = '#666666'
    count_colors = ['#999999', '#0000ff', '#009900', '#ff0000', '#000099',
                    '#990000', '#009999', '#000000', '#990099']
    flag_vertices = np.array([[0.25, 0.2], [0.25, 0.8],
                              [0.75, 0.65], [0.25, 0.5]])  # in data coordinates


    height, width = size

    if height * width != len(puzzle):
        print("error: size does not match puzzle")
        return


    # define internal state variables
    initial = np.zeros((width, height), dtype=bool)  # initial puzzle
    clicked = np.zeros((width, height), dtype=bool)  # user-cleared locations
    flags = np.zeros((width, height), dtype=object)  # marked flags


    def _check_solution():
        sol_str = ""
        k=0
        for j in range(height-1,-1,-1):
          for i in range(width):
            if flags[i, j]:
              sol_str += "1"
            else:
              sol_str += "0"
            k += 1
        if sha256(sol_str.encode()).hexdigest() == solution:
          fig.canvas.mpl_disconnect(cid)   # stops any next event in queue
          fig.set_facecolor("yellow")

    def _toggle_mine_flag(i, j):
        if clicked[i, j] or initial[i, j]:
            pass
        elif flags[i, j]:
            flags[i, j].remove()
            flags[i, j] = None
        else:
            flags[i, j] = plt.Polygon(flag_vertices + [i, j],
                                      fc='red', ec='black', lw=2)
            ax.add_patch(flags[i, j])
            _check_solution()

    def _click_square(i, j, forceClear = False):

        # if there is a flag or square is initially filled, do nothing
        if flags[i, j] or initial[i, j]:
            return

        if forceClear or (not clicked[i, j]):
            clicked[i, j] = True
            squares[i, j].set_facecolor(uncovered_color)
        else:
            clicked[i, j] = False
            squares[i, j].set_facecolor(covered_color)

    def _button_press(event):
        if (event.xdata is None) or (event.ydata is None):
            return
        i, j = int(event.xdata), int(event.ydata)    # in data coordinates
        if (i < 0 or j < 0 or i >= width or j >= height):
            return

        # left mouse button: reveal square.  If the square is initially filled
        # and the correct # of mines are marked, then clear surrounding squares
        if event.button == 1:
            if (initial[i, j]):
                flag_count = flags[max(0, i - 1):i + 2,
                                   max(0, j - 1):j + 2].astype(bool).sum()
                if int(puzzle[ i + width * (height - j - 1) ]) == flag_count:
                    for ii in range(max(0, i - 1), min(width, i + 2)):
                      for jj in range(max(0, j - 1), min(height, j + 2)):
                        _click_square(ii, jj, True)
            else:
                _click_square(i, j)

        # right mouse button: mark/unmark flag
        elif (event.button == 3) and (not initial[i, j]) and (not clicked[i, j]):
            _toggle_mine_flag(i, j)

        fig.canvas.draw()  # redraws everything. Very slow for large puzzles!



    # Create the figure and axes
    fig = plt.figure(figsize=((width + 2) * 31.0 / 100, (height + 2) * 31.0 / 100), dpi=100)
    fig.set_facecolor("#ffffff")    # just to be sure
    ax = fig.add_axes((1.0/(width+2), 1.0/(height+2), width/(width+2.0), height/(height+2.0)),
                      aspect='equal', frameon=False,
                      xlim=(-0.00, width + 0.00),
                      ylim=(-0.00, height + 0.00))
    for axis in (ax.xaxis, ax.yaxis):
        axis.set_major_formatter(plt.NullFormatter())
        axis.set_major_locator(plt.NullLocator())

    # Create the grid of squares
    squares = np.array([[RegularPolygon((i + 0.5, j + 0.5),
                                        numVertices=4,
                                        radius=0.5 * np.sqrt(2),
                                        orientation=np.pi / 4,
                                        ec=edge_color,
                                        fc=covered_color)
                              for j in range(height)]
                             for i in range(width)])
    [ax.add_patch(sq) for sq in squares.flat]

    # put puzzle into figure axis
    k=0
    for j in range(height-1,-1,-1):
      for i in range(width):
        if puzzle[k] != '9':
          initial[i, j] = True
          squares[i, j].set_facecolor(uncovered_color)
          ax.text(i + 0.5, j + 0.5, puzzle[k],
                  color=count_colors[int(puzzle[k])],
                  ha='center', va='center', fontsize=18,
                  fontweight='bold')
        k += 1

    # Create event hook for mouse clicks
    cid = fig.canvas.mpl_connect('button_press_event', _button_press)

    # start!
    plt.show()




MineSweeper(size, puzzle, solution)

