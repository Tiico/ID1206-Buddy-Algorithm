#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <assert.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <math.h>


#define MIN 5
#define LEVELS 8
#define PAGE 4096
#define MAX_NUMBER_OF_PAGES 5

enum flag {Free, Taken};

int allocatedPages = 0;

struct head {
  enum flag status;
  short int level;
  struct head *next;
  struct head *prev;
};

struct head *flists[LEVELS] = {NULL};

struct head *new() {
  struct head *new = (struct head *) mmap(NULL,
                                          PAGE,
                                          PROT_READ | PROT_WRITE,
                                          MAP_PRIVATE | MAP_ANONYMOUS,
                                          -1,
                                          0);
  if (new == MAP_FAILED) {
    return NULL;
  }
  assert(((long int) new & 0xfff) == 0); // 12 last bits should be zero
  new->status = Free;
  new->level = LEVELS - 1;

  allocatedPages++;
  //printf("allocatedPages: %d\n", allocatedPages);
  return new;
}

void add(struct head *block) {
  block->status = Free;
  if (flists[block->level] == NULL) {
    block->next = NULL;
    block->prev = NULL;
    flists[block->level] = block;
  }
  else {
    block->next = flists[block->level];
    block->next->prev = block;
    flists[block->level] = block;
  }
}

struct head *rem(int level) {
  struct head *block;
  if (flists[level] == NULL) {
    return NULL;
  }
  else if (flists[level]->next != NULL) {
    block = flists[level];
    flists[level]->next->prev = NULL;
    flists[level] = flists[level]->next;
  }
  else {
    block = flists[level];
    flists[level] = NULL;
  }
  return block;
}

void remSpec(struct head *block) {
  // MITTEN
  if (block->next != NULL && block->prev != NULL) {
    block->prev->next = block->next;
    block->next->prev = block->prev;
  }
  // FÖRST
  else if (block->next != NULL && block->prev == NULL) {
    flists[block->level] = block->next;
    block->next->prev = NULL;
  }
  // SIST
  else if (block->prev != NULL && block->next == NULL) {
    block->prev->next = NULL;
  }
  // ENSAM
  else {
    flists[block->level] = NULL;
  }
  block->next = NULL;
  block->prev = NULL;
}

struct head *buddy(struct head * block) {
  int index = block->level;
  long int mask = 0x1 << (index + MIN);
  return (struct head*)((long int) block ^ mask);
}

struct head *split(struct head *block) {
  int level = block->level - 1;
  int mask = 0x1 << (level + MIN);
  struct head *split = (struct head *)((long int) block | mask);
  split->level = level;
  block->level = level;
  return split;
}

struct head *primary(struct head * block) {
  int index = block->level;
  long int mask = 0xffffffffffffffff << (1 + index + MIN);
  struct head *primary = (struct head *) ((long int) block & mask);
  primary->level = index + 1;
  return primary;
}

void *hide(struct head* block) {
  return (void *)(block + 1);
}

struct head *magic(void *memory) {
  return ((struct head*) memory - 1);
}

int level(int req) {
  int total = req + sizeof(struct head);

  int i = 0;
  int size = 1 << MIN;
  while (total > size) {
    size <<= 1;
    i += 1;
  }
  return i;
}

struct head *find(int index, int level) {
  if (flists[index] == NULL) {
    if (index == LEVELS - 1) {
      add(new());
      find(index, level);
    }
    else {
      find(index + 1, level);
    }
  }
  else if (index != level) {
    struct head *block = split(rem(index));
    add(block);
    add(buddy(block));
    find(index - 1, level);
  }
  else {
    struct head *block = rem(index);
    block->level = index;
    return block;
  }
}

void *balloc(size_t size) {
  if (size == 0) {
    return NULL;
  }
  int index = level(size);
  struct head *taken = find(index, index);

  taken->status = Taken;
  return hide(taken);
}

void insert(struct head * block) {
  block->status = Free;
  if (block->level == LEVELS - 1) {
    if(allocatedPages <= MAX_NUMBER_OF_PAGES) {
      add(block);
    }
    else {
        munmap(block, 4096);
        allocatedPages--;
      }
  }
  else {
    struct head *bud = buddy(block);
    if ((bud->status == Free) && bud->level == block->level) {
      remSpec(bud);
      insert(primary(block));
    }
    else {
      add(block);
    }
    return;
  }
}

void bfree(void *memory) {
  if (memory != NULL) {
    struct head *block = magic(memory);
    insert(block);
  }
  return;
}

void print_flists() {
  for (int i = LEVELS - 1; i >= 0; i--) {
    printf("flists[%d]:\n", i);
    if (flists[i] != NULL) {
      printf("status -> %d\n", flists[i]->status);
      printf("& -> %p\n", flists[i]);
      printf("level -> %d\n", flists[i]->level);
      printf("next -> %p\n", flists[i]->next);
      printf("prev -> %p\n", flists[i]->prev);
      struct head *block = flists[i]->next;
      while (block != NULL) {
        printf("%4sflists[%d]->next\n", "", i);
        printf("%4sstatus -> %d\n", "", block->status);
        printf("%4s& -> %p\n", "", block);
        printf("%4slevel -> %d\n", "", block->level);
        printf("%4snext -> %p\n", "", block->next);
        printf("%4sprev -> %p\n", "", block->prev);
        block = block->next;
      }
      block = flists[i]->prev;
      while (block != NULL) {
        printf("%6sflists[%d]->prev\n", "", i);
        printf("%6sstatus -> %d\n", "", block->status);
        printf("%6s& -> %p\n", "", block);
        printf("%6slevel -> %d\n", "", block->level);
        printf("%6snext -> %p\n", "", block->next);
        printf("%6sprev -> %p\n", "", block->prev);
        block = block->prev;
      }
    }
    else {
      printf("NULL\n");
    }
    printf("\n");
  }
  printf("\n");
}

void seqBenchmark(int blockSize, int iterations){
  printf("============BENCHMARK SIZE: %d============\n", blockSize);

  clock_t start_balloc, end_balloc, start_bfree, end_bfree, start_malloc, end_malloc, start_free, end_free;
  int *ballocArray[iterations];
  start_balloc = clock();
  for (int i = 0; i < iterations; i++) {
    ballocArray[i] = balloc(blockSize-24);
  }
  end_balloc = clock();

  start_bfree = clock();
  for (int i = 0; i < iterations; i++) {
    bfree(ballocArray[i]);
  }
  end_bfree = clock();
//MALLOC
  int *mallocArray[iterations];
  start_malloc = clock();
  for (int i = 0; i < iterations; i++) {
    mallocArray[i] = malloc(blockSize);
  }
  end_malloc = clock();

  start_free = clock();
  for (int i = 0; i < iterations; i++) {
    free(mallocArray[i]);
  }
  end_free = clock();

  double ballocTime = ((double)(end_balloc - start_balloc))/ CLOCKS_PER_SEC;
  double bfreeTime = ((double)(end_bfree - start_bfree))/ CLOCKS_PER_SEC;
  double mallocTime = ((double)(end_malloc - start_malloc))/ CLOCKS_PER_SEC;
  double freeTime = ((double)(end_free - start_free))/ CLOCKS_PER_SEC;

  double average_ballocTime = ballocTime/iterations*1000000000;
  double average_bfreeTime = bfreeTime/iterations*1000000000;
  double average_mallocTime = mallocTime/iterations*1000000000;
  double average_freeTime = freeTime/iterations*1000000000;



  // printf("Time required for %d Ballocs: %f ms, of size: %d\n", iterations, ballocTime, blockSize);
  // printf("Time required for %d Bfree: %f ms, of size: %d\n", iterations, bfreeTime, blockSize);

  printf("Balloc: Time required when running %d iterations, Average: %.10f ns, Full time: %f ms, of size: %d\n", iterations,  average_ballocTime, ballocTime*1000, blockSize);
  printf("Bfree:  Time required when running %d iterations, Average: %.10f ns, Full time: %f ms, of size: %d\n", iterations,  average_bfreeTime, bfreeTime*1000, blockSize);
  printf("Malloc: Time required when running %d iterations, Average: %.10f ns, Full time: %f ms, of size: %d\n", iterations,  average_mallocTime, mallocTime*1000, blockSize);
  printf("Free:   Time required when running %d iterations, Average: %.10f ns, Full time: %f ms, of size: %d\n", iterations,  average_freeTime, freeTime*1000, blockSize);

  printf("\n");
}

int getSize(int level) {
    int size = pow(2, 5 + level);
    return size;
}
int getMemorySize(void* memory){
  struct head* block = magic(memory);
  switch (block->level) {
    case 0:
      return 32;
    case 1:
      return 64;
    case 2:
      return 128;
    case 3:
      return 256;
    case 4:
      return 512;
    case 5:
      return 1024;
    case 6:
      return 2048;
    case 7:
      return 4096;

  }
}

void randBenchmark(int max_size, int blockRequests){
    srand(time(NULL));
    void *current;
    int rounds = 100;
    int loop = 100;

    int lvl = level(max_size);
    int randomlvl = 0;
    void* buffer[blockRequests];
    int totalusedalloc = 0;
    FILE *file = fopen("allo.dat", "w");
    fprintf(file, "# Rounds: %d, Loops: %d, Buffer: %d\n", rounds, loop, blockRequests);
    fprintf(file, "#Round\tPages amount\tUsed pages\tAlloc pages\tFree memory\n");

    for (int i = 0; i < blockRequests; i++) {
      buffer[i] = NULL;
    }

    for(int j = 0; j < rounds; j++){
    for (int i = 0; i < loop; i++) {
      randomlvl = rand() % (lvl+1);
      int bufferrandom = rand() % blockRequests;
      if (buffer[bufferrandom] != NULL) {
        totalusedalloc -= getMemorySize(buffer[bufferrandom]);
        bfree(buffer[bufferrandom]);
      }
      buffer[bufferrandom] = balloc((getSize(randomlvl))-24);
      totalusedalloc += getMemorySize(buffer[bufferrandom]);
    }


    fprintf(file, "%d\t%d\t%d\t%d\t%d\n", j+1, allocatedPages, totalusedalloc/4096, (allocatedPages*4096)/1000, ((allocatedPages*4096)-totalusedalloc)/1000);
    printf("Total memory used %d kB, Out of allocated: %d kB, Free memory(internal fragmentation): %d kB.\n", totalusedalloc/1000, (allocatedPages*4096)/1000, ((allocatedPages*4096)-totalusedalloc)/1000);
  }
  //print_flists();




}
void test() {
  // for (int i = 32; i <= 4096; i = i*2) {
  //   seqBenchmark(i, 10000);
  // }
    randBenchmark(4000, 1000);

    //print_flists();
  // clock_t teststart, testend;
  // teststart = clock();
  // balloc(4000);
  // testend = clock();
  // double time = ((double)(testend - teststart))/CLOCKS_PER_SEC;
  // printf("%.10f ms\n", time);

  //print_flists();
}
