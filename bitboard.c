#include <stdio.h>
#include <stdint.h>
#include <stdlib.h> //for exit()

#define ROWS 25 //includes bottom solid row
#define MAX_BOARDS 1000000
//#define BLOOM_FILTER_SIZE 172909271 //should be prime. palindrome optional
#define BLOOM_FILTER_SIZE 54218443
//#define BLOOM_FILTER_SIZE 335333533

void GenQueue(uint32_t seed, int pieces[], int piececount);

typedef struct {
    uint16_t rows[ROWS];
} board;

board boards[MAX_BOARDS];
int boardCount = 1;

int bloomFilter[BLOOM_FILTER_SIZE];

void printboard(board* b) {
    for(int i = ROWS-1; i > 0; i--) {
        if(i <= 20) {
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
        if(i <= 20) {
            printf("|");
        }
        printf("\n");
    }
    printf("\\--------------------/\n");
}

/*uint64_t pieces[] = {
0x1e00000000000000, //I horiz
0x1000100010001000, //I vert
0x18000c0000000000, //Z horiz
0x0800180010000000, //Z vert
0x0c00180000000000, //S horiz
0x1000180008000000, //S vert
0x10001c0000000000, //J u
0x1800100010000000, //J r
0x1c00040000000000, //J d
0x0800080018000000, //J l
0x04001c0000000000, //L u
0x1000100018000000, //L r
0x1c00100000000000, //L d
0x1800080008000000, //L l
0x1800180000000000, //O
0x08001c0000000000, //T u
0x1000180010000000, //T r
0x1c00080000000000, //T d
0x0800180008000000  //T l
}; //aligned to the top left of a 4x4 bounding box */

int heights[] = {1, 4, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 2, 3, 2, 3};
int widths[]  = {4, 1, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 2, 3, 2, 3, 2};

uint64_t pieces[] = {
0x0000000000001e00, //I horiz
0x1000100010001000, //I vert
0x0000000018000c00, //Z horiz
0x0000080018001000, //Z vert
0x000000000c001800, //S horiz
0x0000100018000800, //S vert
0x0000000010001c00, //J u
0x0000180010001000, //J r
0x000000001c000400, //J d
0x0000080008001800, //J l
0x0000000004001c00, //L u
0x0000100010001800, //L r
0x000000001c001000, //L d
0x0000180008000800, //L l
0x0000000018001800, //O
0x0000000008001c00, //T u
0x0000100018001000, //T r
0x000000001c000800, //T d
0x0000080018000800  //T l
}; //aligned to the bottom left of a 4x4 bounding box

uint64_t collides(board* b, int piece, int row, int col) {
    //printf("collides %d %d %d %ld %ld\n", piece, row, col, pieces[piece] >> col, *(uint64_t*)(b->rows + row));
    return *(uint64_t*)(b->rows + row) & (pieces[piece] >> col);
}
void place(board* b, int piece, int row, int col) {
    //printf("place %d %d %d %ld %ld\n", piece, row, col, pieces[piece] >> col, *(uint64_t*)(b->rows + row));
    *(uint64_t*)(b->rows + row) |= (pieces[piece] >> col);
}

//TODO line clear function lmao

int prune(board* b) {
    //todo? parity check for the left triangle
    //TODO rewrite with 64-bit masks and unrolled loops
    if(b->rows[1] & 0x1000) { return 1; }
    uint16_t tally = b->rows[1] | 0x1000; //bits get zeroed if there's been bad holes
    if(~tally & b->rows[2]) { return 1; }
    tally &= b->rows[2];
    //TODO enable ITL opener by third row full, remove parity check
    uint16_t mask = 0x1000;
    for(int row = 3; row <= 11; row++) {
        mask >>= 1;
        if(b->rows[row] & mask) { return 1; }
        if((b->rows[row+2] & (mask<<1)) && (~b->rows[row+1] & mask)) { return 1; } //quick overhang hack (bottom diagonal)
        if(~tally & b->rows[row]) { return 1; }
        tally &= b->rows[row] | mask;
    }
    
    if(~tally & b->rows[12]) { return 1; }
    tally &= b->rows[12];
    
    for(int row = 13; row <= 21; row++) {
        mask <<= 1;
        if(b->rows[row] & mask) { return 1; }
        if((b->rows[row+2] & (mask>>1)) && (~b->rows[row+1] & mask)) { return 1; } //quick overhang hack (top diagonal)
        if(~tally & b->rows[row]) { return 1; }
        tally &= b->rows[row] | mask;
    }
    return 0;
}

uint64_t hash(board* b) {
    //TODO: zobrist hashing?
    //this fast (albeit maybe weak) one courtesy of a conversation with Electra
    uint64_t out = 0;
    for(int row = 1; row < ROWS; row += 4) {
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
    place(nb, piece, row, col);
    //printf("pruning %d %d %d %d\n", piece, row, col, prune(b));
    if((b->rows[3] ^ nb->rows[3]) & (b->rows[4] ^ nb->rows[4]) & 0x1000) {
        //quick hack for opening parity
        return;
    }
    if((b->rows[12] ^ nb->rows[12]) & 0x0010 && ~(b->rows[14] ^ nb->rows[14]) & 0x0008) {
        //quick hack for turnaround parity (forces L)
        return;
    }
    if(((b->rows[11] ^ nb->rows[11]) & 0x0030) == 0x0020) {
        //quick hack for impossible turnaround overhang
        return;
    }
    if(!prune(nb) && !bloom(nb)) {
        /*if(boardCount < 20) {
            printboard(nb);
        }*/
        if(nb->rows[21] == 0xEFFF && (nb->rows[22] & 0x1000) && (nb->rows[20] & 0x1000) && (3 <= col && col <= 5)) {
            //TODO account for next piece spawning helping / actually move this code to sg_completes lmao
            printf("sg complete %d\n", boardCount);
            printboard(nb);
        }
        boardCount++;
        if(boardCount >= MAX_BOARDS) {
            printf("boardcount %d exiting\n", boardCount);
            //exit(2);
        }
    }
}

void future(board* b, int piece) {
    //TODO call sg_completes
    for(int col = 0; col <= 10 - widths[piece]; col++) { //TODO start at middle col and move outward
        int row = 21; //bottom left of piece, so this is 21-24, but with 2 full lines that's 19-22 in TAP, which is right. am aware this lets vertical I's get a row too high.
        if(piece == 1) {
            row = 20;
        }
        while(!collides(b, piece, row, col)) {
            row--;
        }
        row++;
        if(row <= 21) {
            newboard(b, piece, row, col);
            if(boardCount >= MAX_BOARDS) { break; }
        }
        if(boardCount >= MAX_BOARDS) { break; }
    }
}

void futures(int firstboard, int lastboard, int piece) {
    for(int i = firstboard; i < lastboard; i++) {
        future(&boards[i], piece);
        if(boardCount >= MAX_BOARDS) { break; }
    }
}

//TODO check for overlapping piece finishing sg
void sg_completes(int firstboard, int lastboard, int piece) {
    for(int i = firstboard; i < lastboard; i++) {
        //future(&boards[i], piece);
        //if(boardCount >= MAX_BOARDS) { break; }
    }
}

void test_pieces() {
    for(int i = 0; i < 19; i++) {
        *(uint64_t*)(boards[i].rows+17) |= pieces[i]; //places at (17, 0)
        printboard(&boards[i]);
    }
}

void test_rng() { //looking for a 5-level topout e.g. IIIJI
    for(int initSeed = 0; initSeed <= 2147483648; initSeed++) {
        int p[5];
        GenQueue(initSeed, p, 5);
        int score = 0;
        for(int i = 0; i < 5; i++) {
            if(p[i]) {
                score++;
                if(score >= 2) {
                    break;
                }
            }
        }
        if(score <= 1) {
            printf("!!!! %d\n", initSeed);
        }
        if(initSeed % 50000000 == 0) {
            printf("seed %d\n", initSeed);
        }
    }
}

void init() {
    //init just the first board with empty rows
    for(int i = 0; i < 1; i++) {
        boards[i].rows[0] = 0xFFFF;     //111 1111111111 111
        for(int j = 1; j < ROWS; j++) {
            boards[i].rows[j] = 0xE007; //111 0000000000 111
        }
    }
    for(int i = 0; i < BLOOM_FILTER_SIZE; i++) {
        bloomFilter[i] = 0;
    }
}

int main() {
    //init();test_pieces();return 0;
    //init();test_rng();return 0;
    //for(int initSeed = 896; initSeed < 1195; initSeed++) { //most common seeds
    //for(int initSeed = 600; initSeed < 1500; initSeed++) {
    for(int initSeed = 1237; initSeed < 1238; initSeed++) { //first test seed
    //for(int initSeed = 1071; initSeed < 1072; initSeed++) { //decent looking seed (blows up)
    //for(int initSeed = 948; initSeed < 949; initSeed++) { //sg?! z end :(
    //for(int initSeed = 1003; initSeed < 1004; initSeed++) { //other sg?! same :(
    //for(int initSeed = 917; initSeed < 918; initSeed++) { //stress test
    //int initSeed = 930; { //another promising seed
        init();
        boardCount = 1;
        int firstboard = 0; //index
        int lastboard = boardCount; // [firstboard, lastboard) for 1 piece bfs
        int p[53];
        GenQueue(initSeed, p, 53);
        //int p[] = {0,6,3,4,1,5,0,3,6,4,5,0,1,6,2,3,4,0,1,5,3,4,6,0,2,5,4,1,3,3,6,0,4,2,1,6,0,5,2,1,6,4,0,2,1,3,4,0,6,1};
        //int p[] = {0,6,4,2,5,1,2,6,4,5,0,1,6,2,5,4,1,0,6,5,3,1,2,6,5,3,0,4,2,6,1,5,4,3,1,6,2,5,0,3,6,2,4,1,5,3,2,0,6,5};
        for(int i = 0; i < 53; i++) {
            switch(p[i]) {
                case 0: //I
                    for(int j = 0; j < 2; j++) {
                        futures(firstboard, lastboard, j);
                    }
                    break;
                case 1: //Z
                    for(int j = 2; j < 4; j++) {
                        futures(firstboard, lastboard, j);
                    }
                    break;
                case 2: //S
                    for(int j = 4; j < 6; j++) {
                        futures(firstboard, lastboard, j);
                    }
                    break;
                case 3: //J
                    for(int j = 6; j < 10; j++) {
                        futures(firstboard, lastboard, j);
                    }
                    break;
                case 4: //L
                    for(int j = 10; j < 14; j++) {
                        futures(firstboard, lastboard, j);
                    }
                    break;
                case 5: //O
                    for(int j = 14; j < 15; j++) {
                        futures(firstboard, lastboard, j);
                    }
                    break;
                case 6: //T
                    for(int j = 15; j < 19; j++) {
                        futures(firstboard, lastboard, j);
                    }
                    break;
                default:
                    printf("oh no!!! %d\n", p[i]);
                    return 1;
            }
            if(boardCount >= MAX_BOARDS) { break; }
            //futures(firstboard, lastboard, p[i]); //noro
            firstboard = lastboard;
            lastboard = boardCount;
            printf("seed %d boardcount %d\n", initSeed, boardCount);
        }
    }
    return 0;
}
