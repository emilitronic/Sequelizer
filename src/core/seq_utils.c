// **********************************************************************
// sequelizer/src/core/seq_utils.c - DNA Sequence Utilities
// **********************************************************************
// Sebastian Claudiusz Magierowski Jun 8 2023
// Moved to sequelizer core: Nov 9 2025

/* A set of functions to help generate random genomic text reads (i.e., composed of
 letters A,C,G,T) as well as extract k-mer equivalents from reads and represent
k-mers as integer indexes representing lexicographical order. */
#include <string.h>
#include <stdlib.h>    // need this for rand
#include <unistd.h>    // on macOS POSIX read() lives in <unistd.h>, not in <stdio.h>
#include <ctype.h>     // for toupper

#include "../../include/sequelizer.h"  // for RETURN_NULL_IF, warnx (via err.h)
#include "kseq.h"      // for KSEQ_INIT
#include "seq_utils.h"

// generates a random string of length len that only
// consists of letters drawn from the alphabet: A,C,G,T
// e.g., generates: AAACAAGCCT
//
// Note: callers are responsible for seeding the global RNG once at
// program startup. This function must not reseed per sequence, or
// synthetic seqgen loops will repeat the same read many times when
// multiple reads are generated within the same second.
char* random_str(int len) {
  char alphabet[] = "ACGT";
  int alphabet_size = strlen(alphabet);

  char* result = (char*)malloc((len + 1) * sizeof(char));
  for (int i = 0; i < len; i++) {
    int random_index = rand() % alphabet_size;
    result[i] = alphabet[random_index];
  }

  result[len] = '\0';

  return result;
}

/* generates a random string of length len that only
 consists of letters drawn from the alphabet: A,C,G,T
and also asks you to choose the random
e.g., generates: GTCTGCCAGC */
char* random_str_seed(int len, unsigned int seed) {
  char alphabet[] = "ACGT";
  int alphabet_size = strlen(alphabet);

  char* result = (char*)malloc((len + 1) * sizeof(char));
  srand(seed); // seed the random number generator
  for (int i = 0; i < len; i++) {
    int random_index = rand() % alphabet_size;
    result[i] = alphabet[random_index];
  }

  result[len] = '\0';

  return result;
}

// This updated function returns an array of num_examples
// random DNA base sequences, each of length len. Remember
// to free the allocated memory after you are done using
// the generated strings.
char** random_str_batch(int len, int num_examples) {
  char alphabet[] = "ACGT";
  int alphabet_size = strlen(alphabet);

  char** results = (char**)malloc(num_examples * sizeof(char*));

  for (int i = 0; i < num_examples; i++) {
    char* result = (char*)malloc((len + 1) * sizeof(char));
    for (int j = 0; j < len; j++) {
      int random_index = rand() % alphabet_size;
      result[j] = alphabet[random_index];
    }
    result[len] = '\0';
    results[i] = result;
  }

  return results;
}

// This updated function returns an array of num_examples
// random DNA base sequences, each of length len. Remember
// to free the allocated memory after you are done using
// the generated strings.
char** random_str_batch_seed(int len, int num_examples, unsigned int seed) {
  char alphabet[] = "ACGT";
  int alphabet_size = strlen(alphabet);

  char** results = (char**)malloc(num_examples * sizeof(char*));
  srand(seed); // seed the random number generator

  for (int i = 0; i < num_examples; i++) {
    char* result = (char*)malloc((len + 1) * sizeof(char));
    for (int j = 0; j < len; j++) {
      int random_index = rand() % alphabet_size;
      result[j] = alphabet[random_index];
    }
    result[len] = '\0';
    results[i] = result;
  }

  return results;
}

// Convert a sequence consisting of letters (drawn from the
// alphabet A,C,G,T) into an equivalent sequences of k-mers
// It dynamically allocates memory for each k-mer and stores
// them in a 2D character array.
// e.g., for sequence=GTCTGCCAGC & k=3 produces
// kmers=GTC,TCT,CTG,TGC,GCC,CCA,CAG,AGC & *num_kmers=8
// use this with free_kmers(kmers, num_kmers)
char** seq_kmers(const char* sequence, int k, int* num_kmers) {
  int sequence_len = strlen(sequence);
  int num_possible_kmers = sequence_len - k + 1;

  char** kmers = (char**)malloc(num_possible_kmers * sizeof(char*));

  for (int i = 0; i < num_possible_kmers; i++) {
    kmers[i] = (char*)malloc((k + 1) * sizeof(char));
    strncpy(kmers[i], sequence + i, k);
    kmers[i][k] = '\0';
  }

  *num_kmers = num_possible_kmers;
  return kmers;
}

// The free_kmers function is provided to deallocate the memory
// allocated for the kmers to avoid memory leaks.
// e.g., use this after using seq_kmers
// or inside a function like seq_kmers_to_ints
void free_kmers(char** kmers, int num_kmers) {
  for (int i = 0; i < num_kmers; i++) {
    free(kmers[i]);
  }
  free(kmers);
}

// Convert a k-mer to an integer in lexicographical order
// (i.e., most-significant letter is on the left)
// It iterates over each character of the kmer from right
// to left and computes the index based on the character's
// position and value.
// e.g. converts ACTG to 180
int kmer_to_int_rev(const char* kmer) {
  int index = 0;
  int kmer_len = strlen(kmer);

  for (int i = kmer_len - 1; i >= 0; i--) {
    index = 4 * index + strchr("ACGT", kmer[i]) - "ACGT";
  }

  return index;
}

// Convert a k-mer to an integer in lexicographical order
// (i.e., most-significant letter is on the left)
// It iterates over each character of the kmer from left
// to right and computes the index based on the character's
// position and value.
// e.g., converts ACTG to 30
int kmer_to_int(const char* kmer) {
  int index = 0;
  int kmer_len = strlen(kmer);

  for (int i = 0; i < kmer_len; i++) {
    index = 4 * index + strchr("ACGT", kmer[i]) - "ACGT";
  }

  return index;
}

// Convert an index to a k-mer of length len
void int_to_kmer(int len, int index, char* kmer) {
  char bases[] = {'A', 'C', 'G', 'T'};
  int i;

  for (i = 0; i < len; ++i) {
    kmer[len-1 - i] = bases[index % (len-1)];
    index /= (len-1);
  }
  kmer[len] = '\0';  // Null-terminate the string
}

// Convert a DNA text sequence (drawn from the alphabet A,C,G,T)
// to an equivalent sequence of k-mers represented by their
// integer equivalent (each integer reflects the lexicographical
// order of each k-mer)
// e.g., for sequence=GTCTGCCAGC & k=3 produces
// GTC,TCT,CTG,TGC,GCC,CCA,CAG,AGC & *num_kmers=8
// ints = 45,55,30,57,37,20,18,9
// sequence: DNA text string
// k: the size of the k-mers that you want to break it into
// num_ints: the number of k-mers in the sequence
// ints: array of integer representations of your kmers
int* seq_kmers_to_ints(const char* sequence, int k, int* num_ints) {
  int num_kmers;
  char** kmers = seq_kmers(sequence, k, &num_kmers); // generates all possible kmers from the sequence

  int* ints = (int*)malloc(num_kmers * sizeof(int));

  for (int i = 0; i < num_kmers; i++) {
    ints[i] = kmer_to_int(kmers[i]); // converts a kmer string to an integer index in lexicographical order
  }

  *num_ints = num_kmers;
  free_kmers(kmers, num_kmers);
  return ints;
}

static int nbase = 4;
KSEQ_INIT(int, read);

/**  Converts a nucleotide base into integer
 *
 *   Input may be uppercase or (optionally) lowercase.  Ambiguous
 *   bases are treated as errors.
 *
 *   a, A -> 0
 *   c, C -> 1
 *   g, G -> 2
 *   t, T -> 3
 *
 *   @param base  Nucleotide to convert
 *   @param allow_lower  Whether to treat lowercase bases as valid input
 *
 *   @returns integer representing base. -1 if base not recognised
 **/
int base_to_int(char base, bool allow_lower){
  base = allow_lower ? toupper(base) : base;
  switch(base){
    case 'A': return 0;
    case 'C': return 1;
    case 'G': return 2;
    case 'T': return 3;
    default:
      warnx("Unrecognised base %d in read", base);
  }
  return -1;
}

/**  Encode an array of nucleotides into integers
 *
 *   @param seq An array of characters containing ASCII encoded
 *   nucleotides (does not explicitly need to be null-terminated).
 *   @param n Length of array `seq`
 *
 *   @returns Array [n] containing encoding of sequence or NULL if an
 *   invalid base was encountered
 **/
int * encode_bases_to_integers(char const * seq, size_t n, size_t state_len){
  const size_t nstate = n - state_len + 1;

  int * iseq = calloc(nstate, sizeof(int));
  RETURN_NULL_IF(NULL == iseq, NULL);
  for(size_t i=0 ; i < nstate ; i++){
    int ib = 0;
    for(size_t j=0 ; j < state_len ; j++){
      int newbase = base_to_int(seq[i + j], true);
      if(-1 == newbase){
        free(iseq);
        return NULL;
      }

      ib *= nbase;
      ib += newbase;
    }
    iseq[i] = ib;
  }

  return iseq;
}
