// **********************************************************************
// core/util.c - Shared Utility Functions
// **********************************************************************
// S Magierowski Aug 23 2025
//

#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>     // For dirname()
#include <limits.h>     // For PATH_MAX

#ifdef __APPLE__
#include <mach-o/dyld.h>  // For _NSGetExecutablePath()
#else
#include <unistd.h>       // For readlink() on Linux
#endif

// **********************************************************************
// Progress Display Functions
// **********************************************************************

void display_progress_simple(int completed, int total, bool verbose, const char *operation) {
  if (total == 0) return;
  
  int percent = (completed * 100) / total;
  int bar_width = 40;
  int filled = (completed * bar_width) / total;
  
  printf("\r[");
  for (int i = 0; i < bar_width; i++) {
    printf(i < filled ? "█" : "░");
  }
  printf("] %d%% (%d/%d)", percent, completed, total);
  
  if (verbose && operation) {
    printf(" %s", operation);
  }

  fflush(stdout);
}

// **********************************************************************
// Path Utilities
// **********************************************************************

// Get the directory where the executable is located
// Returns a newly allocated string that must be freed by caller
// Returns NULL on error
char* get_executable_directory(void) {
  char exe_path[PATH_MAX];

#ifdef __APPLE__
  // macOS: Use _NSGetExecutablePath
  uint32_t size = sizeof(exe_path);
  if (_NSGetExecutablePath(exe_path, &size) != 0) {
    return NULL;
  }

  // Resolve symlinks
  char real_path[PATH_MAX];
  if (realpath(exe_path, real_path) == NULL) {
    return NULL;
  }
#else
  // Linux: Read /proc/self/exe
  ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
  if (len == -1) {
    return NULL;
  }
  exe_path[len] = '\0';

  // Use as-is (already resolved)
  char real_path[PATH_MAX];
  strncpy(real_path, exe_path, sizeof(real_path) - 1);
  real_path[sizeof(real_path) - 1] = '\0';
#endif

  // Get directory part (dirname modifies the string, so copy first)
  char path_copy[PATH_MAX];
  strncpy(path_copy, real_path, sizeof(path_copy) - 1);
  path_copy[sizeof(path_copy) - 1] = '\0';

  char *dir = dirname(path_copy);
  return strdup(dir);
}

// Construct path to kmer_models directory relative to executable
// Returns a newly allocated string that must be freed by caller
// Returns "kmer_models" as fallback if executable directory can't be determined
char* get_default_models_dir(void) {
  char *exe_dir = get_executable_directory();
  if (!exe_dir) {
    // Fallback to relative path if we can't determine executable location
    return strdup("kmer_models");
  }

  // Construct path: <exe_dir>/../kmer_models
  char models_path[PATH_MAX];
  snprintf(models_path, sizeof(models_path), "%s/../kmer_models", exe_dir);
  free(exe_dir);

  // Normalize the path (resolve ../)
  char real_models_path[PATH_MAX];
  if (realpath(models_path, real_models_path) != NULL) {
    return strdup(real_models_path);
  }

  // If realpath fails (directory might not exist yet), return the constructed path
  return strdup(models_path);
}