// **********************************************************************
// test/test_slow5_io.c
// **********************************************************************
// Sebastian Claudiusz Magierowski May 10 2026

#include "core/slow5_io.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  const char *path = argc > 1 ? argv[1] : NULL;
  if (!path) {
    fprintf(stderr, "Usage: %s <file.slow5|file.blow5>\n", argv[0]);
    return 2;
  }

  assert(is_slow5_file(path));

  size_t file_count = 0;
  char **files = find_slow5_files(path, false, &file_count);
  assert(files != NULL);
  assert(file_count == 1);

  size_t read_count = 0;
  slow5_read_t *reads = read_slow5_reads(files[0], &read_count, true);
  assert(reads != NULL);
  assert(read_count > 0);
  assert(reads[0].read_id != NULL);
  assert(reads[0].file_path != NULL);
  assert(reads[0].signal_length > 0);
  assert(reads[0].raw_signal != NULL);
  assert(reads[0].digitisation > 0.0);

  float *pa_signal = slow5_read_to_pa_signal(&reads[0]);
  assert(pa_signal != NULL);

  printf("reads=%zu first_read=%s samples=%llu sample_rate=%.0f\n",
         read_count,
         reads[0].read_id,
         (unsigned long long)reads[0].signal_length,
         reads[0].sample_rate);

  free(pa_signal);
  free_slow5_reads(reads, read_count);
  free_slow5_file_list(files, file_count);

  return 0;
}
