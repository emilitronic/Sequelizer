// **********************************************************************
// core/slow5_io.c - SLOW5/BLOW5 File I/O Operations for Sequelizer
// **********************************************************************
// Sebastian Claudiusz Magierowski May 10 2026

#include "slow5_io.h"

#ifdef SEQUELIZER_HAVE_SLOW5

#include <dirent.h>
#include <err.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>
#include <slow5/slow5.h>

static bool has_suffix(const char *filename, const char *suffix) {
  if (!filename || !suffix) return false;
  size_t filename_len = strlen(filename);
  size_t suffix_len = strlen(suffix);
  return filename_len >= suffix_len &&
         strcmp(filename + filename_len - suffix_len, suffix) == 0;
}

bool is_slow5_file(const char *filename) {
  return has_suffix(filename, ".slow5") || has_suffix(filename, ".blow5");
}

bool is_blow5_file(const char *filename) {
  return has_suffix(filename, ".blow5");
}

static void append_file(char ***files, size_t *count, size_t *capacity, const char *path) {
  if (*count >= *capacity) {
    *capacity = *capacity == 0 ? 1024 : *capacity * 2;
    *files = realloc(*files, *capacity * sizeof(char *));
    if (!*files) {
      errx(EXIT_FAILURE, "Memory allocation failed");
    }
  }

  (*files)[*count] = strdup(path);
  if (!(*files)[*count]) {
    errx(EXIT_FAILURE, "Memory allocation failed");
  }
  (*count)++;
}

char **find_slow5_files_recursive(const char *directory, size_t *count) {
  DIR *dir = NULL;
  struct dirent *entry = NULL;
  struct stat file_stat;
  char path[PATH_MAX];
  char **files = NULL;
  size_t files_capacity = 0;
  *count = 0;

  dir = opendir(directory);
  if (!dir) {
    warnx("Cannot open directory: %s", directory);
    return NULL;
  }

  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.') continue;

    snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
    if (stat(path, &file_stat) != 0) {
      warnx("Cannot stat file: %s", path);
      continue;
    }

    if (S_ISDIR(file_stat.st_mode)) {
      size_t subdir_count = 0;
      char **subdir_files = find_slow5_files_recursive(path, &subdir_count);
      if (subdir_files && subdir_count > 0) {
        for (size_t i = 0; i < subdir_count; i++) {
          append_file(&files, count, &files_capacity, subdir_files[i]);
        }
        free_slow5_file_list(subdir_files, subdir_count);
      }
    } else if (S_ISREG(file_stat.st_mode) && is_slow5_file(entry->d_name)) {
      append_file(&files, count, &files_capacity, path);
    }
  }

  closedir(dir);
  return files;
}

char **find_slow5_files(const char *input_path, bool recursive, size_t *count) {
  struct stat path_stat;
  *count = 0;

  if (!input_path) {
    errx(EXIT_FAILURE, "Input path is NULL");
  }

  if (stat(input_path, &path_stat) != 0) {
    errx(EXIT_FAILURE, "Input path does not exist: %s", input_path);
  }

  if (S_ISREG(path_stat.st_mode)) {
    if (!is_slow5_file(input_path)) {
      errx(EXIT_FAILURE, "Input file is not a SLOW5/BLOW5 file: %s", input_path);
    }

    char **files = malloc(sizeof(char *));
    if (!files) {
      errx(EXIT_FAILURE, "Memory allocation failed");
    }
    files[0] = strdup(input_path);
    if (!files[0]) {
      errx(EXIT_FAILURE, "Memory allocation failed");
    }
    *count = 1;
    return files;
  }

  if (!S_ISDIR(path_stat.st_mode)) {
    errx(EXIT_FAILURE, "Input path is neither a file nor a directory: %s", input_path);
  }

  if (recursive) {
    return find_slow5_files_recursive(input_path, count);
  }

  DIR *dir = opendir(input_path);
  if (!dir) {
    errx(EXIT_FAILURE, "Cannot open directory: %s", input_path);
  }

  char **files = NULL;
  size_t files_capacity = 0;
  struct dirent *entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_name[0] == '.') continue;
    if (!is_slow5_file(entry->d_name)) continue;

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/%s", input_path, entry->d_name);
    if (stat(path, &path_stat) == 0 && S_ISREG(path_stat.st_mode)) {
      append_file(&files, count, &files_capacity, path);
    }
  }

  closedir(dir);
  return files;
}

void free_slow5_file_list(char **files, size_t count) {
  if (!files) return;
  for (size_t i = 0; i < count; i++) {
    free(files[i]);
  }
  free(files);
}

static char *copy_slow5_string(const char *value) {
  return value ? strdup(value) : NULL;
}

static char *copy_aux_string(const slow5_rec_t *rec, const char *field) {
  if (!rec || !rec->aux_map) return NULL;

  int err = 0;
  uint64_t len = 0;
  char *value = slow5_aux_get_string(rec, field, &len, &err);
  if (err != 0 || !value) return NULL;

  char *copy = malloc((size_t)len + 1);
  if (!copy) {
    errx(EXIT_FAILURE, "Memory allocation failed");
  }
  memcpy(copy, value, (size_t)len);
  copy[len] = '\0';
  return copy;
}

static char *copy_header_string(slow5_file_t *sp, const slow5_rec_t *rec, const char *field) {
  if (!sp || !sp->header || !rec) return NULL;
  return copy_slow5_string(slow5_hdr_get(field, rec->read_group, sp->header));
}

static bool copy_signal(int16_t **dest, const slow5_rec_t *rec) {
  *dest = NULL;
  if (!rec || rec->len_raw_signal == 0 || !rec->raw_signal) return true;

  if (rec->len_raw_signal > SIZE_MAX / sizeof(int16_t)) {
    return false;
  }

  *dest = malloc((size_t)rec->len_raw_signal * sizeof(int16_t));
  if (!*dest) {
    return false;
  }

  memcpy(*dest, rec->raw_signal, (size_t)rec->len_raw_signal * sizeof(int16_t));
  return true;
}

static void populate_aux_fields(slow5_read_t *read, const slow5_rec_t *rec) {
  if (!rec || !rec->aux_map) return;

  int err = 0;

  read->channel_number = copy_aux_string(rec, "channel_number");
  read->has_channel_number = read->channel_number != NULL;

  err = 0;
  read->median_before = slow5_aux_get_double(rec, "median_before", &err);
  read->has_median_before = err == 0;

  err = 0;
  read->read_number = slow5_aux_get_int32(rec, "read_number", &err);
  read->has_read_number = err == 0;

  err = 0;
  read->start_mux = slow5_aux_get_uint8(rec, "start_mux", &err);
  read->has_start_mux = err == 0;

  err = 0;
  read->start_time = slow5_aux_get_uint64(rec, "start_time", &err);
  read->has_start_time = err == 0;
}

static bool populate_read(slow5_read_t *read, const char *filename, slow5_file_t *sp,
                          const slow5_rec_t *rec, bool load_signal) {
  read->read_id = copy_slow5_string(rec->read_id);
  read->file_path = copy_slow5_string(filename);
  read->signal_length = rec->len_raw_signal;
  read->read_group = rec->read_group;
  read->digitisation = rec->digitisation;
  read->offset = rec->offset;
  read->range = rec->range;
  read->sample_rate = rec->sampling_rate;

  read->run_id = copy_header_string(sp, rec, "run_id");
  read->flow_cell_id = copy_header_string(sp, rec, "flow_cell_id");
  read->sample_id = copy_header_string(sp, rec, "sample_id");
  read->experiment_name = copy_header_string(sp, rec, "experiment_name");

  populate_aux_fields(read, rec);

  if (load_signal && !copy_signal(&read->raw_signal, rec)) {
    return false;
  }

  return read->read_id != NULL && read->file_path != NULL;
}

slow5_read_t *read_slow5_reads(const char *filename, size_t *read_count, bool load_signal) {
  if (!filename || !read_count) return NULL;
  *read_count = 0;

  slow5_file_t *sp = slow5_open(filename, "r");
  if (!sp) {
    warnx("Failed to open SLOW5/BLOW5 file: %s", filename);
    return NULL;
  }

  slow5_read_t *reads = NULL;
  size_t capacity = 0;
  slow5_rec_t *rec = NULL;
  int ret = 0;

  while ((ret = slow5_get_next(&rec, sp)) >= 0) {
    if (*read_count >= capacity) {
      capacity = capacity == 0 ? 128 : capacity * 2;
      slow5_read_t *new_reads = realloc(reads, capacity * sizeof(slow5_read_t));
      if (!new_reads) {
        slow5_rec_free(rec);
        slow5_close(sp);
        free_slow5_reads(reads, *read_count);
        errx(EXIT_FAILURE, "Memory allocation failed");
      }
      reads = new_reads;
    }

    memset(&reads[*read_count], 0, sizeof(slow5_read_t));
    if (!populate_read(&reads[*read_count], filename, sp, rec, load_signal)) {
      slow5_rec_free(rec);
      slow5_close(sp);
      free_slow5_reads(reads, *read_count + 1);
      warnx("Failed to copy SLOW5 read metadata from: %s", filename);
      return NULL;
    }
    (*read_count)++;
  }

  slow5_rec_free(rec);
  slow5_close(sp);

  if (ret != SLOW5_ERR_EOF) {
    free_slow5_reads(reads, *read_count);
    *read_count = 0;
    warnx("Error while reading SLOW5/BLOW5 file: %s", filename);
    return NULL;
  }

  return reads;
}

void free_slow5_reads(slow5_read_t *reads, size_t count) {
  if (!reads) return;
  for (size_t i = 0; i < count; i++) {
    free(reads[i].read_id);
    free(reads[i].file_path);
    free(reads[i].raw_signal);
    free(reads[i].run_id);
    free(reads[i].flow_cell_id);
    free(reads[i].sample_id);
    free(reads[i].experiment_name);
    free(reads[i].channel_number);
  }
  free(reads);
}

float *slow5_read_to_pa_signal(const slow5_read_t *read) {
  if (!read || !read->raw_signal || read->signal_length == 0) return NULL;
  if (read->digitisation == 0.0) return NULL;
  if (read->signal_length > SIZE_MAX / sizeof(float)) return NULL;

  float *signal = malloc((size_t)read->signal_length * sizeof(float));
  if (!signal) return NULL;

  double scale = read->range / read->digitisation;
  for (uint64_t i = 0; i < read->signal_length; i++) {
    signal[i] = (float)((read->raw_signal[i] + read->offset) * scale);
  }

  return signal;
}

#else

#include <err.h>
#include <stdlib.h>

static void slow5_support_unavailable(void) {
  errx(EXIT_FAILURE, "Sequelizer was built without SLOW5 support");
}

bool is_slow5_file(const char *filename) {
  (void)filename;
  return false;
}

bool is_blow5_file(const char *filename) {
  (void)filename;
  return false;
}

char **find_slow5_files_recursive(const char *directory, size_t *count) {
  (void)directory;
  if (count) *count = 0;
  slow5_support_unavailable();
  return NULL;
}

char **find_slow5_files(const char *input_path, bool recursive, size_t *count) {
  (void)input_path;
  (void)recursive;
  if (count) *count = 0;
  slow5_support_unavailable();
  return NULL;
}

void free_slow5_file_list(char **files, size_t count) {
  (void)count;
  free(files);
}

slow5_read_t *read_slow5_reads(const char *filename, size_t *read_count, bool load_signal) {
  (void)filename;
  (void)load_signal;
  if (read_count) *read_count = 0;
  slow5_support_unavailable();
  return NULL;
}

void free_slow5_reads(slow5_read_t *reads, size_t count) {
  (void)count;
  free(reads);
}

float *slow5_read_to_pa_signal(const slow5_read_t *read) {
  (void)read;
  slow5_support_unavailable();
  return NULL;
}

#endif // SEQUELIZER_HAVE_SLOW5
