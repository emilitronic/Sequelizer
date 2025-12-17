// **********************************************************************
// core/util.h - Shared Utility Functions
// **********************************************************************
// S Magierowski Aug 23 2025
//
#ifndef SEQUELIZER_UTIL_H
#define SEQUELIZER_UTIL_H

#include <stdbool.h>

// **********************************************************************
// Progress Display Functions
// **********************************************************************

// Simple progress bar display with customizable operation text
void display_progress_simple(int completed, int total, bool verbose, const char *operation);

// **********************************************************************
// Path Utilities
// **********************************************************************

// Get the directory where the executable is located
// Returns a newly allocated string that must be freed by caller
// Returns NULL on error
char* get_executable_directory(void);

// Construct path to kmer_models directory relative to executable
// Returns a newly allocated string that must be freed by caller
// Returns "kmer_models" as fallback if executable directory can't be determined
char* get_default_models_dir(void);

#endif // SEQUELIZER_UTIL_H