## How to Run ##

Using gcc (or another C compiler), run the following commands:

```
git clone https://github.com/vungureanu/chess_engine.git
gcc chess_engine/search.c
./chess_engine/a.out -v
```

You should be presented with a graphical representation of a chess board (assuming your systems supports the appropriate Unicode characters).  A square is specified by a letter and a number (e.g., b3); a piece can be moved by concatenating its starting square with its ending square (e.g., entering f1e3 moves the knight in the bottom-right corner up two squares and to the left one square).  After making your move, the system will print the engine's evaluation of its possible moves and make the move it considers to be best.  It will then print the resulting board position, together with some debugging information.

## Documentation ##

### Overview ###

The engine determines the best move in a given position by systematically exploring the tree of variations up to a maximum depth.  Positions at the bottom of this tree are assigned a value based on features such as the material balance, with positions favorable to White being assigned greater values.  Positions not at the bottom of the tree are assigned either the maximum or minimum value among the possible continuations (i.e., positions resulting from making a legal move), depending on whether White or Black is to move.  To whittle down the tree of variations, alpha-beta pruning is used.  This allows branches of the tree that provably cannot result from best play to be discarded (e.g., if White knows he can obtain a draw by playing _x_, but will lose if he plays _y_ and his opponent responds with _z_, White may discard _y_ without considering all potential responses).  The engine may also perform a shallow evaluation of all possible continuations, discarding those it deems sub-optimal.  This is known as forward pruning.  Continuations are explored in decreasing order of their expected value so that more variations may be pruned in this manner.  The evaluation of a position may be stored in a hash table, which later be consulted if the same position is encountered in the exploration of a different variation.  The engine supports multi-threading, with each continuation from a position being allocated one thread.

### Details ###

A position can be represented by an instance of the `Position` or `Compressed_Position` structure.  The former is in a human-readable format, while the latter represents a position as a tuple of three integers (of varying size).  One can convert between these two representations using the functions `compress_position` and `decompress_position`.

Once positions are evaluated, they can be stored toegether with their evaluation in a hash table.  The table uses open addressing with linear probing; for hashing purposes, it has a prime number of elements.  The hash of a position is `prod % p`, where `prod` is the product of the three integers in its compressed representation and `p` is the size of the table.  Each element of the hash table is protected by a mutex lock; to consult the hash table, the engine must first secure this lock.  Consulting the hash table may lead to three outcomes:

* The position is found in the hash table, and has been evaluated at least as deeply as the engine currently proposes to evaluate it.  In this case the engine accepts the evaluation.
* The above is not the case, but room can be made in the hash table for storing the position (i.e., one of the slots corresponding to the position is either free or occupied by a position which was evaluated more shallowly than the current position will be evaluated).  In this case, the engine reserves a slot in the hash table in which it will store the current position, once it has been evaluated.
* None of the above are the case.  The engine ignores the hash table.

The core of the engine is the `find_best_move` function.  It begins by calling `get_moves` to create an array consisting of all those positions which could be obtained from the current position by making a legal move.  `get_moves` ensures that positions resulting from promising moves (e.g., checks or captures) are listed first.  (`get_moves` allows moves to be assigned an integer between 0 and n.  It creates n+1 empty linked lists, the n<sup>th</sup> of which holds all moves assigned the integer n.  After being assigned an integer, moves are added to the head of the appropriate linked list.  They can then be added to the array in the desired order.)  This makes it more likely that the best move will be considered quickly, and that sub-optimal moves will be discarded quickly.

Multiple positions may be evaluted at once.  A semaphore is created whose value is the maximum number of threads that can run concurrently; it is decremented before a thread is created and incremented when a thread terminates.  Each possible continuation is assigned to a thread.
