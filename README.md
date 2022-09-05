# tet-bitboard
A high performance library for Tetris(-likes).  
If you need to search through millions of states per second for a result you want, look no further!  
Initially made for finding a perfect TGM secret grade, but can/will be applied to guideline no-rotate and/or perfect clears.

## Design
This started out as a quick Python program. (and found [this 70-piece no-rotate loop](https://twitter.com/Kirby703_/status/1305555677373280256))  
That version was also used to bash out some solutions for 23 zone lines in Tetris Effect.  
But, it wasn't nearly enough for secret grade! The C version gets a 100-10000 times speedup, which was enough to finish the job.

I didn't love the approach of [this similar project](https://github.com/professor-l/tetris-bitboard) because it seemed like a lot of trouble for a result that's not quite perfect.  
It did inspire me, and after a couple years of thinking on it, I'm glad my new design and implementation works well and can be taken farther.

If you're reading from an era with 256-bit computing, you might represent a board or piece as 24 single-bit fenceposts and 23 10-bit rows, for 254 bits.  
This would let you move pieces by bitshifting (>>1, >>11, <<1, <<11) with line clears as the only pain point.  
But, doing this with a 64-bit computer sucks. Luckily, it's not 32, so I've allocated 16 bits per row, so a piece can fit into a 64-bit int.  
It turns out that (at least on my machine (everyone's least favorite phrase)) you can align 64-bit reads/writes to multiples of 8 bits

Everything else is built on top of that - the algorithm is just "search through every possible board" except we prune ones that will never win, prune ones that don't *look* like they'll win because it's more efficient, and never throw away any boards because memory is not the bottleneck. (CPU time is.)  
Currently, it runs on a single core, but you could divide TGM's seed space across multiple cores if you so choose. This idea may not work as well for other applications, as different cores might stumble across the same board and start duplicating work.  
(Though, to be fair, the de-duplication is a very quick hack that worked just well enough for my purposes anyways, so maybe it's already worth redoing.)

## What's a secret grade?
https://tetris.wiki/Secret_Grade  
It's this side challenge in Tetris where players build a pattern of holes in the shape of a > and then end the game. To "win" at it, a player must fill up 19 rows with one hole each, and make a roof in row 20 that covers the hole in row 19. (one-indexed from the bottom)  
This gets especially challenging near the top, as pieces spawn entirely inside the board, and the game ends if they intersect the stack. In other Tetris games, players can spawn the pieces higher, see farther ahead, and hold one for later, making it easier - but Tetris the Grandmaster 1 and 2 started this challenge at the hardest possible difficulty, to the point that [only 3 people](https://theabsolute.plus/tap/secret-grade) have pulled it off.  
To do this, a line must be cleared at the bottom, and at the middle, meaning a perfect run would only clear 2 lines. Furthermore, it can be done in just 48 tetrominoes (plus one to complete the last cell of the pattern and end the game).

## Results
There are seeds for a perfect secret grade!  
There are 2^25 possible seeds, but they're not distributed evenly. Of 33 million seeds, the first million is by far the most common. On top of that, many players' runs have seeds below 2100 -- it looks like starting a game without going to the title / attract screen is what makes this happen. Of those, the ones near the middle are the most common, just like how if you took the sum of a 4-sided die and a 6-sided die, you'd see a lot more 5's, 6's, and 7's than you'd expect.  
Searching a few billion boards that can be reached via the first 2100 seeds, we see that 58, 85, 245, 276, 552, 581, 582, 583, 633, 656, 699, 1402, 1582, 1654, and 1852 all enable a perfect secret grade. There could be more that use overhangs, tucks, spins, IRS, or an opener that clears row 3 instead of 2, but these results are sufficient for now.
