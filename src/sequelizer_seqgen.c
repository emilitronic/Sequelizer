// **********************************************************************
// sequelizer_seqgen.c
// **********************************************************************
// Sebastian Claudiusz Magierowski Oct 15 2025
/*
Generates signals from read sequences: squiggle, raw, and event sequences.
 squiggle: pos (position along ref), base (from ref at position), current (normalized current), sd (standard deviation of normalized current), dwell (number of samples over which current dwells)
 raw:      is just a model of squiggle in terms of samples with a normal distribution;
 event:    is just a piecewise-constant (i.e., no normal distribution) version of raw

 Example uses:
 List models (from build dir)    sequelizer seqgen --models-dir=../kmer_models --list-models
 Produce squiggle:               sequelizer seqgen ../reads/sebastian_squiggles.fa
 Squiggle to file:               sequelizer seqgen ../reads/sebastian_squiggles.fa -o seb.out
 Produce raw:                    sequelizer seqgen --raw ../reads/sebastian_squiggles.fa
 Alternate sample rate (in kHz): sequelizer seqgen --raw --srate 3 ./reads/sebastian_squiggles.fa
 Produce event:                  sequelizer seqgen --event ./reads/sebastian_squiggles.fa
 Generate raw:                   sequelizer seqgen -g -r -L 200 -o raw_data200.txt
 Generate 5 squiggle:            sequelizer seqgen -g --num-sequences 5 --seq-length 100
 Generate 10 squiggle to file:   sequelizer seqgen --generate --num-sequences 10 --seq-length 50 -o output.txt
 Multiple FASTA to one o/p:      sequelizer seqgen file1.fa file2.fa file3.fa -o combined_output.txt
 Run a size 3 kmer model:        sequelizer seqgen --generate --model rna_r9.4_180mv_70bps --kmer-size 3 --seq-length 10

 Fast5/HDF5 output examples (requires --raw, --fast5, -o):
 Generate single synthetic read:     sequelizer seqgen --raw --fast5 --generate --seq-length 100 -o synthetic.fast5
 Generate multiple synthetic reads:  sequelizer seqgen --raw --fast5 --generate --num-sequences 5 --seq-length 200 -o multi_synthetic.fast5
 Generate data w/ reference:         sequelizer seqgen --raw --fast5 --generate --seq-length=1000 --num-sequences=3 --reference=reference.fa -o signals.fast5
 Convert FASTA to Fast5:             sequelizer seqgen --raw --fast5 ../reads/test_squiggles.fa -o converted.fast5
 Multiple FASTA to Fast5:            sequelizer seqgen --raw --fast5 file1.fa file2.fa file3.fa -o combined.fast5
 Consolidate multiple FASTA in ref:  sequelizer seqgen --raw --fast5 file1.fa file2.fa file3.fa --reference=all_sequences.fa -o combined.fast5
 Fast5 with custom sample rate:      sequelizer seqgen --raw --fast5 --srate 8 ../reads/input.fa -o high_rate.fast5
 Fast5 with read limit:              sequelizer seqgen --raw --fast5 --limit 3 ../reads/many_reads.fa -o limited.fast5
 Limit and multiple i/p files:       sequelizer seqgen --raw --fast5 --limit 100 file1.fa file2.fa file3.fa -o limited.fast5
 Fast5 with reproducible generation: sequelizer seqgen --raw --fast5 --generate --seed 42 --num-sequences 10 -o reproducible.fast5
 Fast5 with kmer model:              sequelizer seqgen --raw --fast5 --generate --model dna_r10.4.1_e8.2_260bps --kmer-size 9 -o kmer.fast5
 Save BOTH fast5 & txt:              sequelizer seqgen --raw --fast5 --save-text --generate --seq-length 50 --num-sequences 1 --reference debug_ref.fa -o debug_signals.fast5

 Read selection examples (--select option):
 Select by name pattern:             sequelizer seqgen --select 6ea6609b reads.fastq -o selected.txt
 Select single read (10th):          sequelizer seqgen --select 10 reads.fastq -o read10.txt
 Select range (reads 101-110):       sequelizer seqgen --select 101:110 reads.fastq -o range.txt
 Select from 10 to end:              sequelizer seqgen --select 10: reads.fastq -o from10.txt
 Select first 12 reads:              sequelizer seqgen --select :12 reads.fastq -o first12.txt
 Select multiple reads (1,5,6):      sequelizer seqgen --select 1,5,6 reads.fastq -o selected.txt
 Select mixed (1-2 and 8-12):        sequelizer seqgen --select 1:2,8:12 reads.fastq -o mixed.txt
 Select by multiple names:           sequelizer seqgen --select 6ea6609b,7b02d4c4 reads.fastq -o names.txt
 Complex selection:                  sequelizer seqgen --select 1,5:10,15,20: reads.fastq -o complex.txt
 Combine with Fast5 output:          sequelizer seqgen --raw --fast5 --select 1,5,6 reads.fastq -o selected.fast5

 Notes:
  - design: Input (FASTA or synthetic) -> sequelizer_seqgen.c (CLI tool) -> seqgen_utils.c (high-level wrapper) ->
            seqgen_models.c (dispatcher) -> squiggle_kmer() (k-mer lookup table) -> 
            kmer_model_loader.c (runtime model loading) -> Output: squiggle -> raw/event -> Fast5/text
  - read names preserved: each read keeps its original FASTA header name
  - limit enforcement: --limit applies across ALL files, not per file

 Current limitation:
  Cannot generate multiple separate output files - the system currently only supports:
  - One output destination (stdout or single file via -o)
  - One output format per run (squiggle OR raw OR event)
*/
#include <stdio.h>
#include <unistd.h>     // on macOS POSIX read() lives in <unistd.h>, not in <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>      // For isdigit()
#include <err.h>
#include <argp.h>
#include <dirent.h>     // For directory scanning (--list_models)
#include <sys/stat.h>   // For stat() to check if directory (--list_models)

#include "core/seqgen_models.h"
#include "core/seqgen_utils.h"
#include "core/seq_utils.h"
#include "core/seq_tensor.h"
#include "core/kseq.h"         // lightweight FASTA/FASTQ parser from klib
#include "core/fast5_io.h"     // Fast5 file writing functions

KSEQ_INIT(int, read) // create a kseq parser that reads from an int fd using the standard C read() system call

// **********************************************************************
// Helper functions for kseq synthetic sequences
// **********************************************************************

// Initialize a kseq_t structure for manual population (without file I/O)
static kseq_t* kseq_init_synthetic(void) {
  kseq_t *seq = calloc(1, sizeof(kseq_t));
  if (seq == NULL) return NULL;

  // Initialize the kstring_t structures
  seq->name.l = seq->name.m = 0;
  seq->name.s = NULL;
  seq->comment.l = seq->comment.m = 0;
  seq->comment.s = NULL;
  seq->seq.l = seq->seq.m = 0;
  seq->seq.s = NULL;
  seq->qual.l = seq->qual.m = 0;
  seq->qual.s = NULL;

  return seq;
}

// Set the sequence name in a kseq_t structure
static void kseq_set_name(kseq_t *seq, const char *name) {
  if (seq == NULL || name == NULL) return;

  size_t len = strlen(name);
  if (seq->name.m < len + 1) {
    seq->name.m = len + 1;
    seq->name.s = realloc(seq->name.s, seq->name.m);
  }
  strcpy(seq->name.s, name);
  seq->name.l = len;
}

// Set the sequence data in a kseq_t structure (takes ownership of sequence string)
static void kseq_set_seq(kseq_t *seq, char *sequence, size_t length) {
  if (seq == NULL) return;

  // Free existing sequence if any
  if (seq->seq.s != NULL) {
    free(seq->seq.s);
  }

  // Take ownership of the provided sequence
  seq->seq.s = sequence;
  seq->seq.l = length;
  seq->seq.m = length + 1;
}

// Clean up a synthetic kseq_t (simpler than full kseq_destroy since no file stream)
static void kseq_destroy_synthetic(kseq_t *seq) {
  if (seq == NULL) return;

  if (seq->name.s) free(seq->name.s);
  if (seq->comment.s) free(seq->comment.s);
  if (seq->seq.s) free(seq->seq.s);
  if (seq->qual.s) free(seq->qual.s);
  free(seq);
}

// **********************************************************************
// Helper functions for model discovery
// **********************************************************************

// Detect k-mer size from a model directory by checking for model files
static int detect_kmer_size(const char *model_path) {
  char file_path[1024];

  // Check for modern format files (9mer, 5mer)
  snprintf(file_path, sizeof(file_path), "%s/9mer_levels_v1.txt", model_path);
  if (access(file_path, F_OK) == 0) return 9;

  snprintf(file_path, sizeof(file_path), "%s/5mer_levels_v1.txt", model_path);
  if (access(file_path, F_OK) == 0) return 5;

  // Check for legacy .model files
  snprintf(file_path, sizeof(file_path), "%s/template_median68pA.model", model_path);
  if (access(file_path, F_OK) != 0) {
    snprintf(file_path, sizeof(file_path), "%s/template_median69pA.model", model_path);
    if (access(file_path, F_OK) != 0) {
      return 0; // No model files found
    }
  }

  // For legacy models, infer k-mer size from directory name
  if (strstr(model_path, "6mer")) return 6;
  if (strstr(model_path, "5mer")) return 5;
  if (strstr(model_path, "9mer")) return 9;

  return 0; // Unknown k-mer size
}

// List all available k-mer models in the models directory
static void list_available_models(const char *models_dir) {
  printf("Available K-mer Models (in %s/):\n\n", models_dir);

  // Open the main models directory
  DIR *dir = opendir(models_dir);
  if (!dir) {
    warnx("Cannot open models directory: %s", models_dir);
    printf("  (No models directory found)\n");
    return;
  }

  int model_count = 0;
  struct dirent *entry;

  // Scan top-level entries
  while ((entry = readdir(dir)) != NULL) {
    // Skip hidden files and . / ..
    if (entry->d_name[0] == '.') continue;

    char model_path[1024];
    snprintf(model_path, sizeof(model_path), "%s/%s", models_dir, entry->d_name);

    struct stat st;
    if (stat(model_path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

    // Check if this is a model directory or contains subdirectories (like legacy/)
    int kmer_size = detect_kmer_size(model_path);

    if (kmer_size > 0) {
      // Direct model directory
      printf("  %s (%d-mer)\n", entry->d_name, kmer_size);
      model_count++;
    } else if (strcmp(entry->d_name, "legacy") == 0) {
      // Special handling for legacy subdirectory
      printf("\n  Legacy models:\n");
      DIR *legacy_dir = opendir(model_path);
      if (legacy_dir) {
        struct dirent *legacy_entry;
        while ((legacy_entry = readdir(legacy_dir)) != NULL) {
          if (legacy_entry->d_name[0] == '.') continue;

          char legacy_model_path[1024];
          snprintf(legacy_model_path, sizeof(legacy_model_path),
                  "%s/%s", model_path, legacy_entry->d_name);

          struct stat legacy_st;
          if (stat(legacy_model_path, &legacy_st) != 0 ||
              !S_ISDIR(legacy_st.st_mode)) continue;

          int legacy_kmer_size = detect_kmer_size(legacy_model_path);
          if (legacy_kmer_size > 0) {
            printf("    legacy/%s (%d-mer)\n",
                   legacy_entry->d_name, legacy_kmer_size);
            model_count++;
          }
        }
        closedir(legacy_dir);
      }
    }
  }
  closedir(dir);

  if (model_count == 0) {
    printf("  (No k-mer models found)\n");
  }

  printf("\nNeural Network Models:\n");
  printf("  squiggle_r94 (not yet implemented)\n");
  printf("  squiggle_r94_rna (not yet implemented)\n");
  printf("  squiggle_r10 (not yet implemented)\n");

  printf("\nDefault model: rna_r9.4_180mv_70bps (5-mer)\n");
  printf("\nUsage:\n");
  printf("  --model <name>           Specify model name\n");
  printf("  --kmer-size <5|6|9>      Specify k-mer size (default: 5)\n");
  printf("  --models-dir <path>      Custom models directory (default: kmer_models)\n");

  printf("\nExamples:\n");
  printf("  sequelizer seqgen --model dna_r10.4.1_e8.2_260bps --kmer-size 9 input.fa\n");
  printf("  sequelizer seqgen --model legacy/legacy_r9.4_180mv_450bps_6mer --kmer-size 6 input.fa\n");
}

// **********************************************************************
// Helper function to count sequences across files
// **********************************************************************

static int count_total_sequences(char **files, int nfile) {
  int total = 0;

  for (int i = 0; i < nfile; i++) {
    FILE *fp = fopen(files[i], "r");
    if (fp == NULL) {
      warnx("Failed to open file for counting: %s", files[i]);
      continue;
    }

    kseq_t *seq = kseq_init(fileno(fp));
    while (kseq_read(seq) >= 0) {
      total++;
    }
    kseq_destroy(seq);
    fclose(fp);
  }

  return total;
}

// **********************************************************************
// Read Selection Infrastructure (--select, -x option)
// **********************************************************************

// Selection modes for filtering reads
enum select_mode {
  SELECT_ALL,      // No filtering (default)
  SELECT_NAME,     // Match by name pattern: --select "6ea6609b"
  SELECT_INDEX,    // Single index: --select "10"
  SELECT_RANGE,    // Range: --select "101:110", "10:", ":12"
};

// Single selection criterion (used for both simple and compound selections)
struct select_criterion {
  enum select_mode mode;
  union {
    char *name_pattern;    // For SELECT_NAME
    struct {               // For SELECT_INDEX and SELECT_RANGE
      int start;           // -1 means "from beginning"
      int end;             // -1 means "to end" (also used for single index)
    } range;
  } params;
};

// Main selection criteria structure (can hold multiple comma-separated criteria)
struct select_criteria {
  struct select_criterion *criteria;  // Array of criteria
  int count;                           // Number of criteria (1 for simple, >1 for comma-separated)
};

// Parse a single selection segment (no commas) into a criterion
// Returns 0 on success, -1 on error
static int parse_single_criterion(const char *segment, struct select_criterion *criterion) {
  if (!segment || !criterion) {
    return -1;
  }

  // Trim leading/trailing whitespace
  while (*segment == ' ' || *segment == '\t') segment++;
  size_t len = strlen(segment);
  while (len > 0 && (segment[len-1] == ' ' || segment[len-1] == '\t')) len--;

  if (len == 0) {
    return -1; // Empty segment
  }

  // Create a clean copy of the segment
  char *clean_segment = strndup(segment, len);
  if (!clean_segment) {
    return -1;
  }

  // Check if string contains ':' (range syntax)
  const char *colon = strchr(clean_segment, ':');

  if (colon != NULL) {
    // RANGE MODE: "start:end", ":end", or "start:"
    criterion->mode = SELECT_RANGE;

    // Parse start index
    if (colon == clean_segment) {
      // ":12" format - from beginning
      criterion->params.range.start = -1;
    } else {
      char start_str[32];
      size_t start_len = colon - clean_segment;
      if (start_len >= sizeof(start_str)) {
        free(clean_segment);
        return -1; // String too long
      }
      strncpy(start_str, clean_segment, start_len);
      start_str[start_len] = '\0';
      criterion->params.range.start = atoi(start_str);
      if (criterion->params.range.start < 1) {
        free(clean_segment);
        return -1; // Invalid start index
      }
    }

    // Parse end index
    if (*(colon + 1) == '\0') {
      // "10:" format - to end
      criterion->params.range.end = -1;
    } else {
      criterion->params.range.end = atoi(colon + 1);
      if (criterion->params.range.end < 1) {
        free(clean_segment);
        return -1; // Invalid end index
      }
    }

    free(clean_segment);
    return 0;
  }

  // Check if string is purely numeric (single index)
  bool is_numeric = true;
  for (const char *p = clean_segment; *p != '\0'; p++) {
    if (!isdigit(*p)) {
      is_numeric = false;
      break;
    }
  }

  if (is_numeric && strlen(clean_segment) > 0) {
    // INDEX MODE: "10"
    criterion->mode = SELECT_INDEX;
    int index = atoi(clean_segment);
    if (index < 1) {
      free(clean_segment);
      return -1; // Invalid index
    }
    criterion->params.range.start = index;
    criterion->params.range.end = index;
    free(clean_segment);
    return 0;
  }

  // NAME MODE: anything else is treated as a name pattern
  criterion->mode = SELECT_NAME;
  criterion->params.name_pattern = clean_segment; // Transfer ownership
  return 0;
}

// Parse selection string (potentially comma-separated) and populate criteria structure
// Returns 0 on success, -1 on error
static int parse_select_criteria(const char *select_str, struct select_criteria *criteria) {
  if (!select_str || !criteria) {
    return -1;
  }

  // Initialize criteria structure
  criteria->criteria = NULL;
  criteria->count = 0;

  // Count commas to estimate number of segments
  int comma_count = 0;
  for (const char *p = select_str; *p != '\0'; p++) {
    if (*p == ',') comma_count++;
  }
  int max_segments = comma_count + 1;

  // Allocate array for criteria
  criteria->criteria = calloc(max_segments, sizeof(struct select_criterion));
  if (!criteria->criteria) {
    return -1;
  }

  // Parse each comma-separated segment
  const char *segment_start = select_str;
  const char *p = select_str;
  int segment_count = 0;

  while (1) {
    if (*p == ',' || *p == '\0') {
      // Found end of segment
      size_t segment_len = p - segment_start;
      if (segment_len > 0) {
        char *segment = strndup(segment_start, segment_len);
        if (!segment) {
          // Cleanup and return error
          for (int i = 0; i < segment_count; i++) {
            if (criteria->criteria[i].mode == SELECT_NAME &&
                criteria->criteria[i].params.name_pattern) {
              free(criteria->criteria[i].params.name_pattern);
            }
          }
          free(criteria->criteria);
          criteria->criteria = NULL;
          criteria->count = 0;
          return -1;
        }

        // Parse this segment
        if (parse_single_criterion(segment, &criteria->criteria[segment_count]) == 0) {
          segment_count++;
        }
        free(segment);
      }

      if (*p == '\0') break;
      segment_start = p + 1;
    }
    p++;
  }

  criteria->count = segment_count;

  if (segment_count == 0) {
    free(criteria->criteria);
    criteria->criteria = NULL;
    return -1; // No valid segments found
  }

  return 0;
}

// Check if a read matches a single criterion
// read_index is 1-based (first read = 1)
static bool matches_criterion(const struct select_criterion *criterion,
                              const char *read_name,
                              int read_index) {
  if (!criterion) {
    return false;
  }

  switch (criterion->mode) {
    case SELECT_ALL:
      return true;

    case SELECT_NAME:
      // Check if read name contains the pattern
      if (read_name && criterion->params.name_pattern) {
        return strstr(read_name, criterion->params.name_pattern) != NULL;
      }
      return false;

    case SELECT_INDEX:
      // Single index match
      return (read_index == criterion->params.range.start);

    case SELECT_RANGE:
      // Range check
      if (criterion->params.range.start != -1 && read_index < criterion->params.range.start) {
        return false; // Before range start
      }
      if (criterion->params.range.end != -1 && read_index > criterion->params.range.end) {
        return false; // After range end
      }
      return true;

    default:
      return false;
  }
}

// Check if a read should be processed based on selection criteria
// Returns true if read matches ANY criterion (OR logic for comma-separated selections)
// read_index is 1-based (first read = 1)
static bool should_process_read(const struct select_criteria *criteria,
                                const char *read_name,
                                int read_index) {
  if (!criteria || criteria->count == 0) {
    return true; // No criteria = process all
  }

  // Check if read matches ANY criterion (OR logic)
  for (int i = 0; i < criteria->count; i++) {
    if (matches_criterion(&criteria->criteria[i], read_name, read_index)) {
      return true;
    }
  }

  return false; // No criteria matched
}

// Clean up selection criteria (free allocated memory)
static void free_select_criteria(struct select_criteria *criteria) {
  if (!criteria || !criteria->criteria) {
    return;
  }

  // Free each criterion in the array
  for (int i = 0; i < criteria->count; i++) {
    if (criteria->criteria[i].mode == SELECT_NAME &&
        criteria->criteria[i].params.name_pattern) {
      free(criteria->criteria[i].params.name_pattern);
      criteria->criteria[i].params.name_pattern = NULL;
    }
  }

  // Free the array itself
  free(criteria->criteria);
  criteria->criteria = NULL;
  criteria->count = 0;
}

// **********************************************************************
// Argument Parsing
// **********************************************************************
static char doc[] = "sequelizer seqgen -- Signal generation from DNA sequence reads\v"
"EXAMPLES:\n"
"  sequelizer seqgen reads.fa\n"
"  sequelizer seqgen -g --num-sequences 5 --seq-length 100\n"
"  sequelizer seqgen --list-models\n"
"  sequelizer seqgen --model dna_r10.4.1_e8.2_260bps --kmer-size 9 reads.fa";

static char args_doc[] = "fasta [fasta ...]";

static struct argp_option options[] = {
  {"model",         'm', "name",       0, "K-mer model name (e.g., 'rna_r9.4_180mv_70bps', 'dna_r10.4.1_e8.2_260bps')"},
  {"models-dir",    'd', "path",       0, "K-mer models directory (default: 'kmer_models')"},
  {"kmer-size",     'k', "size",       0, "K-mer size for k-mer model (default: 5)"},
  {"limit",         'l', "nreads",     0, "Maximum number of reads to call (0 is unlimited)"},
  {"select",        'x', "pattern",    0, "Select reads: name, index (10), range (1:10), or comma-separated (1,5,6 or 1:2,8:12)"},
  {"output",        'o', "filename",   0, "Write to file rather than stdout"},
  {"prefix",        'p', "string",     0, "Prefix to append to name of each read"},
  {"rescale",        1,  0,            0, "Rescale network output"},
  {"no-rescale",     2,  0, OPTION_ALIAS, "Don't rescale network output"},
  {"raw",           'r', 0,            0, "Generate raw signal from squiggle events"},
  {"event",         'e', 0,            0, "Generate event signal from squiggle"},
  {"fast5",         'f', 0,            0, "Output signals in Fast5/HDF5 format (requires --raw and -o)"},
  {"srate",         's', "rate",       0, "Sampling rate in kHz (default: 4.0)"},
  {"generate",      'g', 0,            0, "Generate synthetic sequences instead of reading from file"},
  {"seq-length",    'L', "length",     0, "Length of generated sequences in bases (default: 100)"},
  {"num-sequences", 'N', "count",      0, "Number of sequences to generate (default: 1)"},
  {"seed",          'S', "seed",       0, "Random seed for reproducible generation (optional)"},
  {"reference",     'R', "filename",   0, "Save sequences to reference FASTA file (use with --generate or multiple inputs)"},
  {"save-text",     'T', 0,            0, "Also save text format when using --fast5 (creates .txt companion file)"},
  {"list-models",   'M', 0,            0, "List available k-mer models and exit"},
  {0}
};

struct arguments {
  char *model_name;
  char *models_dir;
  int kmer_size;
  int limit;
  struct select_criteria select;
  FILE *output;
  char *output_filename;
  char *prefix;
  bool rescale;
  bool generate_raw;
  bool generate_event;
  bool generate_sequences;
  bool output_fast5;
  int seq_length;
  int num_sequences;
  unsigned int seed;
  bool use_seed;
  float sample_rate_khz;
  char *reference_filename;
  FILE *reference_file;
  bool save_text;
  char **files;
};

static int parse_arg(int key, char *arg, struct argp_state *state) {
  struct arguments *arguments = state->input;

  switch (key) {
    case 'm':
      arguments->model_name = arg;
      break;
    case 'd':
      arguments->models_dir = arg;
      break;
    case 'k':
      arguments->kmer_size = atoi(arg);
      if (arguments->kmer_size <= 0 || arguments->kmer_size > 9) {
        errx(EXIT_FAILURE, "K-mer size must be between 1 and 9, got %d", arguments->kmer_size);
      }
      break;
    case 'l':
      arguments->limit = atoi(arg);
      if (arguments->limit < 0) {
        errx(EXIT_FAILURE, "Limit must be non-negative, got %d", arguments->limit);
      }
      break;
    case 'x':
      if (parse_select_criteria(arg, &arguments->select) != 0) {
        errx(EXIT_FAILURE, "Invalid selection pattern: %s", arg);
      }
      break;
    case 'o':
      arguments->output_filename = arg;
      arguments->output = fopen(arg, "w");
      if (NULL == arguments->output) {
        errx(EXIT_FAILURE, "Failed to open \"%s\" for output.", arg);
      }
      break;
    case 'p':
      arguments->prefix = arg;
      break;
    case 1:
      arguments->rescale = true;
      break;
    case 2:
      arguments->rescale = false;
      break;
    case 'r':
      arguments->generate_raw = true;
      break;
    case 'e':
      arguments->generate_event = true;
      break;
    case 'f':
      arguments->output_fast5 = true;
      break;
    case 's':
      arguments->sample_rate_khz = atof(arg);
      if (arguments->sample_rate_khz <= 0.0) {
        errx(EXIT_FAILURE, "Sampling rate must be positive, got %f", arguments->sample_rate_khz);
      }
      break;
    case 'g':
      arguments->generate_sequences = true;
      break;
    case 'L':
      arguments->seq_length = atoi(arg);
      if (arguments->seq_length <= 0) {
        errx(EXIT_FAILURE, "Sequence length must be positive, got %d", arguments->seq_length);
      }
      break;
    case 'N':
      arguments->num_sequences = atoi(arg);
      if (arguments->num_sequences <= 0) {
        errx(EXIT_FAILURE, "Number of sequences must be positive, got %d", arguments->num_sequences);
      }
      break;
    case 'S':
      arguments->seed = (unsigned int)atoi(arg);
      arguments->use_seed = true;
      break;
    case 'R':
      arguments->reference_filename = arg;
      arguments->reference_file = fopen(arg, "w");
      if (NULL == arguments->reference_file) {
        errx(EXIT_FAILURE, "Failed to open reference file \"%s\" for writing", arg);
      }
      break;
    case 'T':
      arguments->save_text = true;
      break;
    case 'M':
      // List models and exit
      list_available_models(arguments->models_dir);
      exit(0);
    case ARGP_KEY_NO_ARGS:
      if (!arguments->generate_sequences) {
        argp_usage(state);
      }
      break;
    case ARGP_KEY_ARG:
      arguments->files = &state->argv[state->next - 1];
      state->next = state->argc;
      break;
    default:
      return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static struct argp argp = { options, parse_arg, args_doc, doc };

// **********************************************************************
// Main Function
// **********************************************************************
int main_seqgen(int argc, char *argv[]) {

  // ========================================================================
  // STEP 1: Initialize arguments structure with defaults
  // ========================================================================
  struct arguments arguments;

  // Set sensible defaults for all configuration options
  arguments.model_name = "rna_r9.4_180mv_70bps";  // Small 5-mer default for testing
  arguments.models_dir = "kmer_models";
  arguments.kmer_size = 5;
  arguments.limit = 0;
  arguments.select.criteria = NULL;  // No filtering by default
  arguments.select.count = 0;
  arguments.output = NULL;
  arguments.output_filename = NULL;
  arguments.prefix = "";
  arguments.rescale = true;
  arguments.generate_raw = false;
  arguments.generate_event = false;
  arguments.generate_sequences = false;
  arguments.output_fast5 = false;
  arguments.seq_length = 100;
  arguments.num_sequences = 1;
  arguments.seed = 0;
  arguments.use_seed = false;
  arguments.sample_rate_khz = 4.0;
  arguments.reference_filename = NULL;
  arguments.reference_file = NULL;
  arguments.save_text = false;
  arguments.files = NULL;

  // ========================================================================
  // STEP 2: Parse command line arguments
  // ========================================================================
  argp_parse(&argp, argc, argv, 0, 0, &arguments);

  // Validate --fast5 constraints
  if (arguments.output_fast5) {
    if (!arguments.generate_raw) {
      errx(EXIT_FAILURE, "--fast5 flag requires --raw flag (cannot output squiggle or event modes to Fast5)");
    }
    if (NULL == arguments.output_filename) {
      errx(EXIT_FAILURE, "--fast5 flag requires -o output filename (cannot output Fast5 to stdout)");
    }
  }

  // Validate --save-text constraints
  if (arguments.save_text && !arguments.output_fast5) {
    errx(EXIT_FAILURE, "--save-text flag requires --fast5 flag (text output is automatic without --fast5)");
  }

  // Set up text output file for --save-text mode
  if (arguments.save_text) {
    // Create companion text file by replacing .fast5 extension with .txt
    char *text_filename = malloc(strlen(arguments.output_filename) + 10);
    strcpy(text_filename, arguments.output_filename);
    char *ext = strrchr(text_filename, '.');
    if (ext && strcmp(ext, ".fast5") == 0) {
      strcpy(ext, ".txt");
    } else {
      strcat(text_filename, ".txt");
    }
    arguments.output = fopen(text_filename, "w");
    if (NULL == arguments.output) {
      errx(EXIT_FAILURE, "Failed to open text output file \"%s\"", text_filename);
    }
    free(text_filename);
  } else if (NULL == arguments.output) {
    // Default to stdout if no output file specified
    arguments.output = stdout;
  }

  // ========================================================================
  // STEP 3: Initialize counters and pointers, determine loop parameters
  // ========================================================================
  int reads_started = 0;
  const int reads_limit = arguments.limit;

  // Determine loop count and Fast5 capacity based on mode
  int num_iterations;  // Total number of sequences to process
  int nfile = 0;       // Number of input files (file-based mode only)
  int fast5_capacity;  // Initial capacity for Fast5 arrays

  // File-based mode: need to track current file and sequence parser
  FILE **file_handles = NULL;
  kseq_t **file_parsers = NULL;
  int current_file_idx = 0;
  kseq_t *current_seq = NULL;

  if (arguments.generate_sequences) {
    // SYNTHETIC MODE: loop count = number of sequences to generate
    num_iterations = arguments.num_sequences;
    fast5_capacity = (reads_limit > 0) ? reads_limit : num_iterations;
  } else {
    // FILE-BASED MODE: count files and total sequences
    for (; arguments.files[nfile]; nfile++); // count files

    // Pre-count total sequences across all files
    int total_sequences = count_total_sequences(arguments.files, nfile);
    num_iterations = total_sequences;
    fast5_capacity = (reads_limit > 0) ? reads_limit : total_sequences;

    // Open all files and initialize parsers
    file_handles = calloc(nfile, sizeof(FILE*));
    file_parsers = calloc(nfile, sizeof(kseq_t*));

    // Open files & create kseq parsers
    for (int i = 0; i < nfile; i++) {
      file_handles[i] = fopen(arguments.files[i], "r");
      if (NULL == file_handles[i]) {
        warnx("Failed to open \"%s\" for input", arguments.files[i]);
        continue;
      }
      file_parsers[i] = kseq_init(fileno(file_handles[i]));
    }

    // Find first valid file & select its kseq_t parser
    current_file_idx = 0;
    while (current_file_idx < nfile && file_parsers[current_file_idx] == NULL) {
      current_file_idx++;
    }
    if (current_file_idx < nfile) {
      current_seq = file_parsers[current_file_idx];
    }
  }

  // Add dwell time tracking variables
  float total_dwell_time = 0.0f;
  size_t total_positions = 0;

  // Initialize random seed if requested
  if (arguments.use_seed) {
    srand(arguments.seed);
  }

  // Fast5 mode: prepare arrays to collect data
  seq_tensor **fast5_raw_signals = NULL;
  const char **fast5_read_names = NULL;
  int fast5_read_count = 0;

  if (arguments.output_fast5) {
    fast5_raw_signals = calloc(fast5_capacity, sizeof(seq_tensor*));
    fast5_read_names = calloc(fast5_capacity, sizeof(char*));
    if (NULL == fast5_raw_signals || NULL == fast5_read_names) {
      errx(EXIT_FAILURE, "Failed to allocate memory for Fast5 data collection");
    }
  }

  // ========================================================================
  // STEP 4: FLAT UNIFIED PROCESSING LOOP (handles both synthetic and file-based modes)
  // ========================================================================
  for (int i = 0; i < num_iterations; i++) {
    // Respect user-specified read limit
    if (reads_limit > 0 && reads_started >= reads_limit) {
      break;
    }
    reads_started += 1;

    // ======================================================================
    // STEP 4.1: Obtain next sequence (either synthetic or from file)
    // ======================================================================
    kseq_t *seq = NULL;
    bool is_synthetic = false;

    if (arguments.generate_sequences) {
      // SYNTHETIC MODE: Create a new sequence
      seq = kseq_init_synthetic();
      if (NULL == seq) {
        errx(EXIT_FAILURE, "Failed to allocate kseq_t for synthetic sequence %d", i + 1);
      }
      is_synthetic = true;

      // Generate synthetic DNA sequence and populate kseq_t
      char *generated_sequence = random_str(arguments.seq_length);
      if (NULL == generated_sequence) {
        kseq_destroy_synthetic(seq);
        errx(EXIT_FAILURE, "Failed to generate synthetic sequence %d", i + 1);
      }

      // Populate kseq_t fields
      char seq_name[32];
      snprintf(seq_name, sizeof(seq_name), "generated_%03d", i + 1);
      kseq_set_name(seq, seq_name);
      kseq_set_seq(seq, generated_sequence, arguments.seq_length);
    } else {
      // FILE-BASED MODE: Read next sequence from current file
      // Find next file with sequences if current file is exhausted
      while (current_file_idx < nfile) {
        if (current_seq != NULL && kseq_read(current_seq) >= 0) {
          seq = current_seq;
          break;
        }
        // Current file exhausted, move to next file
        current_file_idx++;
        if (current_file_idx < nfile) {
          current_seq = file_parsers[current_file_idx];
        }
      }

      if (seq == NULL) {
        // No more sequences available
        break;
      }
    }

    // ======================================================================
    // STEP 4.2: Apply read selection filtering (--select / -x option)
    // ======================================================================
    // Check if this read should be processed based on selection criteria
    // Note: reads_started is 1-based (first read = 1)
    if (!should_process_read(&arguments.select, seq->name.s, reads_started)) {
      // Skip this read - don't process it
      if (is_synthetic) {
        kseq_destroy_synthetic(seq);
      }
      continue; // Move to next read
    }

    // Write sequence to reference file if requested
    if (arguments.reference_file != NULL) {
      if (seq->name.l > 0) {
        fprintf(arguments.reference_file, ">%s\n%s\n", seq->name.s, seq->seq.s);
      } else {
        fprintf(arguments.reference_file, ">sequence_%d\n%s\n", reads_started, seq->seq.s);
      }
    }

    // Debug output: show sequence length
    printf("seq length %zu\n", seq->seq.l);

    // ======================================================================
    // STEP 4.3: Set up model parameters for k-mer dispatcher
    // ======================================================================
    struct seqgen_model_params model_params = {
      .model_type = SEQGEN_MODEL_KMER,
      .params = {
        .kmer = {
          .model_name = arguments.model_name,
          .models_dir = arguments.models_dir,
          .kmer_size = arguments.kmer_size,
          .sample_rate_khz = arguments.sample_rate_khz
        }
      }
    };

    // ======================================================================
    // STEP 4.4: Generate squiggle data from sequence
    // This calls sequence_to_squiggle() -> dispatcher -> squiggle_kmer()
    // ======================================================================
    seq_tensor *squiggle = sequence_to_squiggle(
      seq->seq.s,
      seq->seq.l,
      arguments.rescale,
      &model_params
    );

    if (NULL != squiggle) {
      // Write sequence identifier to output (skip for Fast5 mode unless save_text is enabled)
      if (!arguments.output_fast5 || arguments.save_text) {
        fprintf(arguments.output, "#%s\n", seq->name.s);
      }

      // ====================================================================
      // STEP 4.5: Generate final output based on user's requested format
      // ====================================================================
      if (arguments.generate_raw) {
        // RAW MODE: Convert squiggle events to time-series samples with Gaussian noise
        seq_tensor *raw_signal = squiggle_to_raw(squiggle, arguments.sample_rate_khz);
        if (NULL != raw_signal) {
          if (arguments.output_fast5) {
            // Fast5 mode: check capacity and expand if needed
            if (fast5_read_count >= fast5_capacity) {
              fast5_capacity *= 2;
              fast5_raw_signals = realloc(fast5_raw_signals, fast5_capacity * sizeof(seq_tensor*));
              fast5_read_names = realloc(fast5_read_names, fast5_capacity * sizeof(char*));
              if (NULL == fast5_raw_signals || NULL == fast5_read_names) {
                errx(EXIT_FAILURE, "Failed to expand Fast5 data arrays");
              }
            }

            // Store the tensor and read name (we own these now)
            fast5_raw_signals[fast5_read_count] = raw_signal;
            fast5_read_names[fast5_read_count] = strdup(seq->name.s);
            fast5_read_count++;

            // Also output text if save_text is enabled
            if (arguments.save_text) {
              fprintf(arguments.output, "sample_index\traw_value\n");
              float *raw_data = seq_tensor_data_float(raw_signal);
              size_t num_samples = seq_tensor_dim(raw_signal, 0);
              for (size_t j = 0; j < num_samples; j++) {
                fprintf(arguments.output, "%zu\t%3.6f\n", j, raw_data[j]);
              }
            }
          } else {
            // Text mode: output to file/stdout then free
            fprintf(arguments.output, "sample_index\traw_value\n");
            float *raw_data = seq_tensor_data_float(raw_signal);
            size_t num_samples = seq_tensor_dim(raw_signal, 0);
            for (size_t j = 0; j < num_samples; j++) {
              fprintf(arguments.output, "%zu\t%3.6f\n", j, raw_data[j]);
            }
            seq_tensor_free(raw_signal);
          }
        }
      } else if (arguments.generate_event) {
        // EVENT MODE: Convert squiggle to piecewise-constant signal (no noise)
        seq_tensor *event_signal = squiggle_to_event(squiggle, arguments.sample_rate_khz);
        if (NULL != event_signal) {
          fprintf(arguments.output, "sample_index\tevent_value\n");
          float *event_data = seq_tensor_data_float(event_signal);
          size_t num_samples = seq_tensor_dim(event_signal, 0);
          for (size_t j = 0; j < num_samples; j++) {
            fprintf(arguments.output, "%zu\t%3.6f\n", j, event_data[j]);
          }
          seq_tensor_free(event_signal);
        }
      } else {
        // SQUIGGLE MODE (default): Output the three squiggle features
        fprintf(arguments.output, "pos\tbase\tcurrent\tsd\tdwell\n");
        float *data = seq_tensor_data_float(squiggle);
        size_t num_positions = seq_tensor_dim(squiggle, 0);

        for (size_t j = 0; j < num_positions; j++) {
          float current = data[j * 3 + 0];
          float stddev = data[j * 3 + 1];
          float dwell_time = data[j * 3 + 2];

          fprintf(arguments.output, "%zu\t%c\t%3.6f\t%3.6f\t%3.6f\n",
                  j,
                  seq->seq.s[j],
                  current,
                  stddev,
                  dwell_time);

          // Accumulate dwell time statistics
          total_dwell_time += dwell_time;
          total_positions++;
        }
      }

      seq_tensor_free(squiggle);
    }

    // Clean up sequence structure (only for synthetic sequences)
    if (is_synthetic) {
      kseq_destroy_synthetic(seq);
    }
  } // end for loop

  // ========================================================================
  // STEP 5: CLEANUP AND FINALIZATION (common to both modes)
  // ========================================================================

  // Clean up file-based mode resources
  if (!arguments.generate_sequences && file_parsers != NULL) {
    for (int i = 0; i < nfile; i++) {
      if (file_parsers[i] != NULL) {
        kseq_destroy(file_parsers[i]);
      }
      if (file_handles[i] != NULL) {
        fclose(file_handles[i]);
      }
    }
    free(file_parsers);
    free(file_handles);
  }

  // Print average dwell time statistics if we processed sequences in SQUIGGLE mode
  if (!arguments.generate_raw && !arguments.generate_event) {
    if (total_positions > 0) {
      float average_dwell_time = total_dwell_time / total_positions;
      size_t total_samples = (size_t)ceil(average_dwell_time * total_positions);
      printf("Average dwell time: %.6f (across %zu positions and %zu samples)\n",
             average_dwell_time, total_positions, total_samples);
    } else {
      printf("No sequences processed - no dwell time data available\n");
    }
  }

  // Report reference file writing if enabled
  if (arguments.reference_file != NULL) {
    fflush(arguments.reference_file);
    if (arguments.generate_sequences) {
      printf("Wrote %d generated sequences to reference file: %s\n",
             reads_started, arguments.reference_filename);
    } else {
      printf("Wrote %d sequences from input files to reference file: %s\n",
             reads_started, arguments.reference_filename);
    }
  }

  // Write Fast5 file if requested
  if (arguments.output_fast5 && fast5_read_count > 0) {
    if (fast5_read_count == 1) {
      seq_write_fast5_single(arguments.output_filename,
                            fast5_raw_signals,
                            fast5_read_names,
                            fast5_read_count,
                            arguments.sample_rate_khz);
    } else {
      seq_write_fast5_multi(arguments.output_filename,
                           fast5_raw_signals,
                           fast5_read_names,
                           fast5_read_count,
                           arguments.sample_rate_khz);
    }

    printf("Wrote %d reads to Fast5 file: %s\n",
           fast5_read_count, arguments.output_filename);

    // Clean up Fast5 data
    for (int i = 0; i < fast5_read_count; i++) {
      if (fast5_raw_signals[i]) {
        seq_tensor_free(fast5_raw_signals[i]);
      }
      if (fast5_read_names[i]) {
        free((void*)fast5_read_names[i]);
      }
    }
    free(fast5_raw_signals);
    free(fast5_read_names);
  }

  // Close reference file if it was opened
  if (arguments.reference_file != NULL) {
    fclose(arguments.reference_file);
    printf("Closed reference file: %s\n", arguments.reference_filename);
  }

  // Close text output file if we opened it for --save-text
  if (arguments.save_text && arguments.output != stdout) {
    fclose(arguments.output);
  }

  // Clean up selection criteria
  free_select_criteria(&arguments.select);

  return EXIT_SUCCESS;
}
