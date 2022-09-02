# tet-bitboard
A high performance library for Tetris(-likes).\
If you need to search through millions of states per second for a result you want, look no further!\
Initially made for finding a perfect TGM secret grade, but can/will be applied to guideline no-rotate and/or perfect clears.

Started as a quick Python program. (found the 70-piece no-rotate loop that https://twitter.com/Kirby703_/status/1305555677373280256 has a gif of)\
That was also used to bash out some solutions for 23 zone lines in Tetris Effect.\
But, it wasn't nearly enough for secret grade! The C version gets a 100-10000 times speedup.

I didn't love the approach of https://github.com/professor-l/tetris-bitboard because it seemed like a lot of trouble for a result that's not quite perfect.\
It did inspire me, and after a couple years of thinking on it, I'm glad my new design and implementation works well and can be taken farther.

If you're reading from an era with 256-bit computing, you might represent a board or piece as 24 single-bit fenceposts and 23 10-bit rows, for 254 bits.\
This would let you move pieces by bitshifting (>>1, >>11, <<1, <<11) with line clears as the only pain point.\
But, doing this with a 64-bit computer sucks. Luckily, it's not 32, so I've allocated 16 bits per row, so a piece can fit into a 64-bit int.\
It turns out that (at least on my machine (everyone's least favorite phrase)) you can align reads/writes of 64-bit ints to 16 bits without the same issues as 1 bit or 8 bit alignment.

Everything else is built on top of that - the algorithm is just "search through every possible board" except you prune ones that will never win, prune ones that don't *look* like they'll win because it's more efficient, and never throw away any boards because memory is not the bottleneck. (CPU time is)\
Currently, it runs on a single core, but you could divide TGM's seed space across multiple cores if you so choose. This idea may not work as well for other applications, as different cores might stumble across the same board and start duplicating work.\
(Though, to be fair, the de-duplication is a very quick hack that worked just well enough for my purposes anyways, so maybe it's already worth redoing.)
