## How to Run ##

Using gcc (or another C compiler), run the following commands:

```
git clone https://github.com/vungureanu/chess_engine.git
gcc chess_engine/search.c
./chess_engine/a.out -v
```

You should be presented with a graphical representation of a chess board (assuming your systems supports the appropriate Unicode characters).  A square is specified by a letter and a number (e.g., b3); a piece can be moved by concatenating its starting square with its ending square (e.g., entering f1e3 moves the knight in the bottom-right corner up two squares and to the left one square).  After making your move, the system will print the engine's evaluation of its possible moves and make the move it considers to be best.  It will then print the resulting board position, together with some debugging information.

## Documentation ##

## Overview ##

The engine explores variations up to a maximum depth.  Once it reaches that depth, it calls an evaluation function to judge the position; positions that are more favorable to White are given higher values.  Depending on the current depth, the engine may perform a shallow evaluation of all possible continuations (i.e., positions obtainable from the current position by making a legal move), discarding those it deems sub-optimal.  This is known as forward pruning.  The present position is assigned the value of the most- or least- valuable continuation, depending on whether White or Black is to move.  A continuation need not be evaluated fully; it can be discarded once it is known to be worse than an alternative continuation (e.g., if White knows he can obtain a draw by playing _x_, but will lose if he plays _y_ and his opponent responds with _z_, he may discard _y_ without considering all potential responses to _y_).  This is known as alpha-beta pruning.  Continuations are evaluated in order of their expected value to increase the number of variations pruned in this manner.  A hash table is used to store the evaluations of various positions; this table can be consulted when evaluating a position.  Also, the engine supports multi-threading; multiple continuations can be evaluated at the same time (although this decreases the usefulness of the hash table).

The core of the engine is the `find_best_move` function.  It begins by calling `get_moves` to create an array consisting of all those positions which could be obtained from the current position by making a legal move.  `get_moves` ensures that positions resulting from promising moves (e.g., checks or captures) are listed first.  (`get_moves` allows moves to be assigned an integer between 0 and n.  It creates n+1 empty linked lists, the n<sup>th</sup> of which holds all moves assigned the integer n.  After being assigned an integer, moves are added to the head of the appropriate linked list.  They can then be added to the array in the desired order.)  This makes it more likely that the best move will be considered quickly, and that sub-optimal moves will be discarded quickly.
