#include <stdio.h>
#include <stdint.h>
#include <stdlib.h> //for exit()

//compile with -O2 !!!
//it inlines the functions that are split up for readability.
//-O3 breaks it (not place() or collides() as expected, but something with de-duplication)
//haven't tested if -funroll-loops would also help (so, TODO)
//also I didn't go ham optimizing the outermost loops because they're just not that important

#define ROWS 28 //includes bottom solid row. need 24 (21 for board, 2 for full lines, 1 for bottom wall) but 32 (or 28 + parent) just makes cache hits happen *so much better* that it's worth the +33% memory and cpu time to copy boards around

#define QUEUE_LENGTH 49 //for secret grade: 19*9 + 2 minos on the board, +20 for line clears, +3 for a finish = 196 minos = 49 tetrominoes minimum. 53 allows for many leftover minos in rows 20 and 21, if you just want a 2-line-clear secret GM rather than a perfect 50-level run.

#define MAX_BOARDS 33000000 //not a beam search! you will have this many boards in total rather than per step.
//downside compared to beam search: takes up 5-10 times as much memory, which is for naught if you only care about if a winning outcome exists
//upside: saves all intermediate results so you don't have to run the program again or write to disk to get the sequence of moves corresponding to the good outcome

#define BLOOM_FILTER_SIZE 335333533 //just has to be prime. 172909271 was nice but the larger, the better

// ======================= START OF TGM2 RNG ======================= //
typedef enum Block {
    I, Z, S, J, L, O, T
} Block;

uint8_t LCG(uint32_t* seed) {
    *seed = 0x41C64E6D * *seed + 12345;
    return ((*seed >> 10) & 0x7FFF) % 7;
}

void GenQueue(uint32_t seed, int queue[], int queueLength) {
    uint8_t history[4];
    uint8_t next = Z;
    while(next == Z || next == S || next == O) {
        next = LCG(&seed);
    }
    history[0] = next;
    history[1] = Z;
    history[2] = S;
    history[3] = S;
    queue[0] = next;

    for(int block = 1; block < queueLength; block++) {
        for (int rolls = 0; rolls < 5; rolls++) {
            next = LCG(&seed);
            
            char reroll = 0;
            for (int i = 0; i < 4; i++) {
                if (next == history[i]) {
                    reroll = 1;
                }
            }

            if (reroll) {
                next = LCG(&seed); //you'd think this would be a nop but it's accurate to TGM2
            } else {
                break;
            } 
        }
        history[3] = history[2];
        history[2] = history[1];
        history[1] = history[0];
        history[0] = next;
        queue[block] = next;
    }
}
// ======================== END OF TGM2 RNG ======================== //

//here be global variable dragons

typedef struct board {
    uint16_t rows[ROWS];
    struct board* parent;
} board;
_Static_assert(sizeof(board) == 64, "sizeof(board) != 64 which is a problem for cache hits");

board boards[MAX_BOARDS] __attribute__ ((aligned (64)));
int boardCount = 1;

int bloomFilter[BLOOM_FILTER_SIZE];

void printBoard(board* b) {
    for(int i = 23; i > 0; i--) {
        if(i <= 22) {
            printf("|");
        } else {
            printf(" ");
        }
        for(uint16_t mask = 0x1000; mask >= 0x0008; mask >>= 1) {
            if(b->rows[i] & mask) {
                printf("[]");
            } else {
                printf("..");
            }
        }
        if(i <= 22) {
            printf("|");
        }
        printf("\n");
    }
    printf("\\--------------------/\n");
}

//https://tetris.wiki/ARS
//implemented as a 4x16 rectangle with 4 bits set where the piece is "centered" in the leftmost 4x4
uint64_t pieces[] = {
0x0000f00000000000, //I horiz ( - )
0x2000200020002000, //I vert  ( | )
0x0000c00060000000, //Z horiz (like the letter)
0x2000600040000000, //Z vert
0x00006000c0000000, //S horiz (like the letter)
0x8000c00040000000, //S vert
0x00008000e0000000, //J u
0x6000400040000000, //J r
0x0000e00020000000, //J d
0x40004000c0000000, //J l (like the letter)
0x00002000e0000000, //L u
0x4000400060000000, //L r (like the letter)
0x0000e00080000000, //L d
0xc000400040000000, //L l
0x0000600060000000, //O
0x00004000e0000000, //T u
0x4000600040000000, //T r
0x0000e00040000000, //T d (like the letter)
0x4000c00040000000  //T l
};
int map[] = {0, 2, 4, 6, 10, 14, 15, 19}; //index into 19 pieces[] by the 7 tetromino types
int spawnOrientation[] = {0, 2, 4, 8, 12, 14, 17}; //same

//reading/writing 64 bits to a 16 bit aligned location is fine on most [citation definitely needed] 64-bit machines, or at least intels
uint64_t collides(board* b, int piece, int row, int col) {
    return *(uint64_t*)(b->rows + row) & (pieces[piece] >> col);
}
void place(board* b, int piece, int row, int col) {
    *(uint64_t*)(b->rows + row) |= (pieces[piece] >> col);
}

//TODO a line clear function would be nice to extend this from tgm secret grade to a no rotate / pc finder

int shouldPrune(board* b, board* nb) {
    //this whole function is unavoidably specific to 2-line-clear secret grade!
    //for guideline no-rotate, one might instead choose to prune any boards with a 1-wide hole

    //TODO enable ITL opener by letting the third row be full instead of the second, and fix the green check accordingly
    //see colors in https://fumen.zui.jp/?v115@JeB8IeJ8AeJ8AeJ8AeJ8AeJ8AeJ8AeJ8AeA8g0H8Ae?I8g0H8hlAeH8AeH8AeH8AeH8AeH8AeH8AeF8Q4A8AeG8Q4A?eR8AeI8JeAgH
    if((b->rows[3] ^ nb->rows[3]) & (b->rows[4] ^ nb->rows[4]) & 0x1000) {
        return 1; //both green: check for opening parity 1 mod 4
    }
    if((b->rows[12] ^ nb->rows[12]) & 0x0010 && ~(b->rows[14] ^ nb->rows[14]) & 0x0008) {
        return 1; //bottom blue without top blue: check for turnaround parity 2 mod 4 (forces J)
    }
    if(((b->rows[11] ^ nb->rows[11]) & 0x0030) == 0x0020) {
        return 1; //left orange without right orange: check for impossible turnaround overhang
    }
    
    //TODO? this throws out boards with buried holes, but in doing so efficiently, it also throws out valid boards that require tucks or spins
    //"tally" handles this, moving up the board and getting a bit zeroed if there's an empty cell in that column (aside from an intentional sg hole) and then if it encounters a filled cell with a zeroed bit, we know that the board has an overhang or hole and it gets discarded
    if(nb->rows[1] & 0x1000) { return 1; }
    uint16_t tally = nb->rows[1] | 0x1000;
    
    //full row
    if(~tally & nb->rows[2]) { return 1; } //created an overhang/hole
    tally &= nb->rows[2];
    
    uint16_t mask = 0x1000;
    for(int row = 3; row <= 11; row++) {
        mask >>= 1;
        if(nb->rows[row] & mask) { return 1; } //mistakenly filled an sg hole
        if((nb->rows[row+2] & (mask<<1)) && (~nb->rows[row+1] & mask)) { return 1; } //impossible lack of overhang (bottom diagonal)
        if(~tally & nb->rows[row]) { return 1; } //created an overhang/hole
        tally &= nb->rows[row] | mask;
    }
    
    //full row
    if(~tally & nb->rows[12]) { return 1; }
    tally &= nb->rows[12];
    
    for(int row = 13; row <= 21; row++) {
        mask <<= 1;
        if(nb->rows[row] & mask) { return 1; }
        if((nb->rows[row+2] & (mask>>1)) && (~nb->rows[row+1] & mask)) { return 1; } //same check as above (top diagonal)
        if(~tally & nb->rows[row]) { return 1; }
        tally &= nb->rows[row] | mask;
    }
    
    return 0;
}

//hash and bloom are a quick hack to eliminate most duplicate boards

uint64_t hash(board* b) {
    //TODO: zobrist hashing?
    //this fast (albeit maybe weak) hash courtesy of a conversation with Electra
    uint64_t out = 0;
    for(int row = 0; row < 24; row += 4) {
        out ^= *(uint64_t*)(b->rows + row);
    }
    return out;
}

int bloom(board* b) {
    //TODO k > 1
    uint64_t h = hash(b) % BLOOM_FILTER_SIZE;
    int out = bloomFilter[h];
    if(out) {
        for(int i = 1; i < ROWS; i++) {
            if(boards[out].rows[i] != b->rows[i]) {
                bloomFilter[h] = boardCount; //figure the old false-match is out of date / has fewer pieces
                return 0;
            }
        }
        return 1;
    } else {
        bloomFilter[h] = boardCount;
        return 0;
    }
}

void newboard(board* b, int piece, int row, int col) {
    board* nb = &boards[boardCount];
    for(int i = 0; i < ROWS; i++) {
        nb->rows[i] = b->rows[i];
    }
    nb->parent = b;
    place(nb, piece, row, col);
    if(!shouldPrune(b, nb) && !bloom(nb)) {
        boardCount++;
        if(boardCount >= MAX_BOARDS) {
            printf("boardcount %d exiting\n", boardCount);
            exit(1);
        }
    }
}

int sgComplete(board* b, int piece) {
    //check for completed secret grade
    //only need to check the top rows; if anything at the bottom were wrong it would have been pruned
    if(collides(b, piece, 20, 6)) {
        place(b, piece, 20, 6);
        if(b->rows[20] == 0xF7FF && b->rows[21] == 0xEFFF && (b->rows[22] & 0x1000)) {
            printf("sg complete %d\n", boardCount);
            printBoard(b);
            while(b->parent) {
                b = b->parent;
                printBoard(b);
                //TODO why print so many boards
            }
        }
        return 1;
    }
    return 0;
}

void future(board* b, int piece) {
    //TODO? this double loop idea feels bad and really branchy! but maybe it doesn't actually take up a significant portion of the runtime?
    for(int col = 6; !collides(b, piece, 20, col); col--) { //at 6, the very first for loop check will always succeed - hopefully the compiler optimizes it away
        int row = 19; //20 was free so drop it by 1
        while(!collides(b, piece, row, col)) {
            row--;
        }
        row++; //can bump back up to 20
        newboard(b, piece, row, col);
    }
    for(int col = 7; !collides(b, piece, 20, col); col++) {
        int row = 19;
        while(!collides(b, piece, row, col)) {
            row--;
        }
        row++;
        newboard(b, piece, row, col);
    }
}

void init() {
    //init just the first board with empty rows as others will copy
    boards[0].rows[0] = 0xFFFF;     //111 1111111111 111
    for(int i = 1; i < ROWS; i++) {
        boards[0].rows[i] = 0xE007; //111 0000000000 111
    }
    boards[0].parent = 0;
    for(int i = 0; i < BLOOM_FILTER_SIZE; i++) {
        bloomFilter[i] = 0;
    }
}

void displayPieces() {
    for(int i = 0; i < 19; i++) {
        place(&boards[i], i, 18, 6); //18-21, correct for TAP (assuming no full lines)
        printBoard(&boards[i]);
    }
}

int main() {
    for(int initSeed = 0; initSeed <= 2100; initSeed++) {
        init();
        boardCount = 1;
        int firstboard = 0; //index into boards
        int lastboard = boardCount; // [firstboard, lastboard) for 1 step of bfs
        int p[QUEUE_LENGTH];
        GenQueue(initSeed, p, QUEUE_LENGTH);
        for(int i = 0; i < QUEUE_LENGTH; i++) {
            for(int j = firstboard; j < lastboard; j++) {
                if(!sgComplete(&boards[j], spawnOrientation[p[i]])) {
                    for(int k = map[p[i]]; k < map[p[i]+1]; k++) {
                        future(&boards[j], k);
                    }
                }
            }
            firstboard = lastboard;
            lastboard = boardCount;
            printf("seed %d boardcount %d\n", initSeed, boardCount);
        }
    }
    return 0;
}
