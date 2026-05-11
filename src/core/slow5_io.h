// **********************************************************************
// core/slow5_io.h - SLOW5/BLOW5 File I/O Operations for Sequelizer
// **********************************************************************
// Sebastian Claudiusz Magierowski May 10 2026
// Optional slow5lib-backed helpers. Compile with SEQUELIZER_HAVE_SLOW5.

#ifndef SEQUELIZER_SLOW5_IO_H
#define SEQUELIZER_SLOW5_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef SEQUELIZER_HAVE_SLOW5
#include <slow5/slow5.h>
#else
typedef struct slow5_file slow5_file_t;
typedef struct slow5_rec slow5_rec_t;
#endif

typedef struct {
  char *read_id;
  char *file_path;

  int16_t *raw_signal;
  uint64_t signal_length;

  uint32_t read_group;
  double digitisation;
  double offset;
  double range;
  double sample_rate;

  char *run_id;
  char *flow_cell_id;
  char *sample_id;
  char *experiment_name;

  char *channel_number;
  double median_before;
  int32_t read_number;
  uint8_t start_mux;
  uint64_t start_time;

  bool has_channel_number;
  bool has_median_before;
  bool has_read_number;
  bool has_start_mux;
  bool has_start_time;
} slow5_read_t;

typedef void (*slow5_read_enhancer_t)(slow5_file_t *sp,
                                      const slow5_rec_t *rec,
                                      slow5_read_t *read);

bool is_slow5_file(const char *filename);
bool is_blow5_file(const char *filename);

char **find_slow5_files_recursive(const char *directory, size_t *count);
char **find_slow5_files(const char *input_path, bool recursive, size_t *count);
void free_slow5_file_list(char **files, size_t count);

void extract_slow5_header_fields(slow5_file_t *sp, const slow5_rec_t *rec, slow5_read_t *read);
void extract_slow5_aux_fields(slow5_file_t *sp, const slow5_rec_t *rec, slow5_read_t *read);

slow5_read_t *read_slow5_reads_with_enhancer(const char *filename,
                                             size_t *read_count,
                                             bool load_signal,
                                             slow5_read_enhancer_t enhancer);
slow5_read_t *read_slow5_reads(const char *filename, size_t *read_count, bool load_signal);
void free_slow5_reads(slow5_read_t *reads, size_t count);

float *slow5_read_to_pa_signal(const slow5_read_t *read);

#endif // SEQUELIZER_SLOW5_IO_H
