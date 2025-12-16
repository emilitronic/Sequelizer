// **********************************************************************
// core/fast5_convert.c - Fast5 Format Conversion Functions
// **********************************************************************
// Sebastian Claudiusz Magierowski Aug 22 2025
//

#include "fast5_convert.h"
#include "fast5_io.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <err.h>
#include <hdf5.h>

// **********************************************************************
// Utility Functions
// **********************************************************************

// Helper function to try extracting channel from filename as fallback
static void try_filename_channel_extraction(const char *filename, fast5_metadata_t *metadata) {
  if (!filename || metadata->channel_number) return; // Already have channel or no filename
  
  const char *basename = strrchr(filename, '/');
  basename = basename ? basename + 1 : filename;
  
  // Look for patterns like "ch271" or "channel271"  
  const char *ch_pos = strstr(basename, "ch");
  if (ch_pos) {
    int channel_num = 0;
    if (sscanf(ch_pos, "ch%d", &channel_num) == 1 && channel_num > 0 && channel_num < 10000) {
      metadata->channel_number = malloc(16);
      if (metadata->channel_number) {
        snprintf(metadata->channel_number, 16, "%d", channel_num);
      }
    }
  }
}

// Simple enhancer to extract channel number from Fast5 files
static void extract_channel_number(hid_t file_id, hid_t signal_dataset_id, fast5_metadata_t *metadata) {
  // Initialize
  metadata->channel_number = NULL;
  
  if (signal_dataset_id < 0) return;
  
  // Get the dataset path to determine format
  char obj_name[256];
  ssize_t name_len = H5Iget_name(signal_dataset_id, obj_name, sizeof(obj_name));
  if (name_len <= 0) return;
  
  
  
  hid_t channel_group_id = -1;
  
  if (strstr(obj_name, "/Raw/Reads/")) {
    // Single-read format: /Raw/Reads/Read_xxx/Signal -> /UniqueGlobalKey/channel_id
    channel_group_id = H5Gopen2(file_id, "/UniqueGlobalKey/channel_id", H5P_DEFAULT);
  } else if (strstr(obj_name, "/Raw/Signal")) {
    // Multi-read format: /read_<UUID>/Raw/Signal -> /read_<UUID>/channel_id
    char channel_path[512];
    char *read_part = strstr(obj_name, "/read_");
    if (read_part) {
      char *raw_part = strstr(read_part, "/Raw");
      if (raw_part) {
        size_t read_len = raw_part - obj_name;
        snprintf(channel_path, sizeof(channel_path), "%.*s/channel_id", (int)read_len, obj_name);
        channel_group_id = H5Gopen2(file_id, channel_path, H5P_DEFAULT);
      }
    }
  }
  
  if (channel_group_id >= 0) {
    // Try to read channel_number attribute
    hid_t attr_id = H5Aopen(channel_group_id, "channel_number", H5P_DEFAULT);
    if (attr_id >= 0) {
      hid_t attr_type = H5Aget_type(attr_id);
      H5T_class_t type_class = H5Tget_class(attr_type);
      
      if (type_class == H5T_INTEGER) {
        int channel_num;
        if (H5Aread(attr_id, H5T_NATIVE_INT, &channel_num) >= 0) {
          metadata->channel_number = malloc(16);
          if (metadata->channel_number) {
            snprintf(metadata->channel_number, 16, "%d", channel_num);
          }
        }
      } else if (type_class == H5T_STRING) {
        size_t str_size = H5Tget_size(attr_type);
        
        if (str_size == 8) {
          // This might be a binary-encoded integer
          unsigned char channel_bytes[8];
          if (H5Aread(attr_id, attr_type, channel_bytes) >= 0) {
            // Try different interpretations
            uint32_t as_uint32_le = *((uint32_t*)channel_bytes);
            uint32_t as_uint32_be = __builtin_bswap32(*((uint32_t*)channel_bytes));
            uint16_t as_uint16_le = *((uint16_t*)channel_bytes);
            uint16_t as_uint16_be = __builtin_bswap16(*((uint16_t*)channel_bytes));
            
            // Pick the most reasonable channel number (should be 1-512 for most nanopore devices)
            uint32_t channel_num = 0;
            if (as_uint16_le > 0 && as_uint16_le < 1000) {
              channel_num = as_uint16_le;
            } else if (as_uint16_be > 0 && as_uint16_be < 1000) {
              channel_num = as_uint16_be;
            } else if (as_uint32_le > 0 && as_uint32_le < 1000) {
              channel_num = as_uint32_le;
            } else if (as_uint32_be > 0 && as_uint32_be < 1000) {
              channel_num = as_uint32_be;
            }
            
            if (channel_num > 0) {
              metadata->channel_number = malloc(16);
              if (metadata->channel_number) {
                snprintf(metadata->channel_number, 16, "%u", channel_num);
              }
            }
          }
        } else if (str_size > 0 && str_size < 32) {
          char channel_str[32] = {0}; // Initialize to zeros
          if (H5Aread(attr_id, attr_type, channel_str) >= 0) {
            // Check if it's a null-terminated text string
            bool is_text = true;
            for (size_t i = 0; i < str_size && channel_str[i] != '\0'; i++) {
              if (channel_str[i] < 32 || channel_str[i] > 126) { // Non-printable ASCII
                is_text = false;
                break;
              }
            }
            if (is_text) {
              channel_str[str_size] = '\0'; // Ensure null termination
              metadata->channel_number = malloc(str_size + 1);
              if (metadata->channel_number) {
                strcpy(metadata->channel_number, channel_str);
              }
            }
          }
        }
      }
      
      H5Tclose(attr_type);
      H5Aclose(attr_id);
    }
    H5Gclose(channel_group_id);
  }
}

// Combined enhancer that extracts both channel number and calibration parameters
static void extract_channel_and_calibration_combined(hid_t file_id, hid_t signal_dataset_id, fast5_metadata_t *metadata) {
  // First extract channel number using existing function
  extract_channel_number(file_id, signal_dataset_id, metadata);
  
  // Then extract calibration parameters using the new function from fast5_io.c
  extract_calibration_parameters(file_id, signal_dataset_id, metadata);
}

int write_signal_to_file(const char *filename, float *signal, size_t signal_length, const fast5_metadata_t *metadata) {
  FILE *f = fopen(filename, "w");
  if (!f) {
    warnx("Cannot create output file: %s", filename);
    return EXIT_FAILURE;
  }
  
  // Write metadata header if available
  if (metadata) {
    // Channel information
    if (metadata->channel_number) {
      fprintf(f, "# Channel: %s\n", metadata->channel_number);
    }
    
    // Calibration parameters (if available)
    if (metadata->calibration_available) {
      fprintf(f, "# Offset: %.6f\n", metadata->offset);
      fprintf(f, "# Range: %.6f\n", metadata->range);
      fprintf(f, "# Digitisation: %.6f\n", metadata->digitisation);
      fprintf(f, "# Conversion: signal_pA = (raw_signal + offset) * range / digitisation\n");
    }
    
    // Additional metadata
    if (metadata->sample_rate > 0) {
      fprintf(f, "# Sample Rate: %.1f\n", metadata->sample_rate);
    }

    if (metadata->duration > 0) {
      fprintf(f, "# Duration: %u\n", metadata->duration);
    }

    if (metadata->read_id) {
      fprintf(f, "# Read ID: %s\n", metadata->read_id);
    }
    
    // Separator line
    fprintf(f, "#\n");
  }
  
  // Write column headers
  fprintf(f, "sample_index\traw_sample\n");

  // Write signal data in two-column format
  for (size_t i = 0; i < signal_length; i++) {
    // Raw ADC values are integers, so cast and print without decimals
    fprintf(f, "%zu\t%d\n", i, (int)signal[i]);
  }
  
  fclose(f);
  return EXIT_SUCCESS;
}

int create_directory(const char *path) {
  struct stat st = {0};
  
  // Check if directory already exists
  if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) {
    return EXIT_SUCCESS;
  }
  
  // Try to create directory
  if (mkdir(path, 0755) != 0) {
    warnx("Cannot create directory: %s", path);
    return EXIT_FAILURE;
  }
  
  return EXIT_SUCCESS;
}

// **********************************************************************
// Signal Extraction Functions
// **********************************************************************

int extract_raw_signals(char **files, size_t file_count, const char *output_file, 
                         bool all_reads, bool verbose) {
  if (verbose) {
    printf("Converting %zu files to raw format...\n", file_count);
  }
  
  // Show initial progress bar (only for multiple files)
  if (file_count > 1) {
    display_progress_simple(0, (int)file_count, verbose, "converting files");
  }
  
  for (size_t i = 0; i < file_count; i++) {
    if (verbose) {
      printf("Processing file: %s\n", files[i]);
    }
    
    // Read metadata to determine format and get read IDs with channel numbers and calibration
    size_t metadata_count = 0;
    fast5_metadata_t *metadata = read_fast5_metadata_with_enhancer(files[i], &metadata_count, extract_channel_and_calibration_combined);
    
    if (!metadata || metadata_count == 0) {
      warnx("Cannot read metadata from file: %s", files[i]);
      continue;
    }
    
    // Try filename extraction as fallback for any missing channel numbers
    for (size_t j = 0; j < metadata_count; j++) {
      if (!metadata[j].channel_number) {
        try_filename_channel_extraction(files[i], &metadata[j]);
      }
    }
    
    // Handle single-read vs multi-read files
    bool is_multi_read = metadata[0].is_multi_read;
    size_t reads_to_process = metadata_count;
    
    // For multi-read files, limit to first 3 reads unless --all specified
    if (is_multi_read && !all_reads && metadata_count > 3) {
      reads_to_process = 3;
      if (verbose) {
        printf("  Multi-read file: processing first 3 of %zu reads (use --all for all)\n", metadata_count);
      }
    }
    
    // Show progress bar for single multi-read file with many reads
    bool show_read_progress = (file_count == 1 && is_multi_read && reads_to_process > 10);
    if (show_read_progress) {
      display_progress_simple(0, (int)reads_to_process, verbose, "extracting reads");
    }
    
    // Create output directory if needed and we're on the first file
    if (output_file && i == 0) {
      // Only create directory if:
      // 1. Multiple files (file_count > 1), OR
      // 2. Single multi-read file (file_count == 1 && is_multi_read)
      // Don't create directory for single single-read files (they use exact filename)
      if (file_count > 1 || (file_count == 1 && is_multi_read)) {
        if (create_directory(output_file) != EXIT_SUCCESS) {
          free_fast5_metadata(metadata, metadata_count);
          continue;
        }
      }
    }
    
    // Extract signals
    for (size_t j = 0; j < reads_to_process; j++) {
      size_t signal_length = 0;
      float *signal = read_fast5_signal(files[i], metadata[j].read_id, &signal_length);
      
      if (!signal || signal_length == 0) {
        if (verbose) {
          printf("  Failed to extract signal for read: %s\n", 
                 metadata[j].read_id ? metadata[j].read_id : "unknown");
        }
        continue;
      }
      
      // Determine output filename
      char output_filename[512];

      // Get basename for prefixing when needed
      const char *basename = strrchr(files[i], '/');
      basename = basename ? basename + 1 : files[i];

      if (file_count == 1) {
        // Single file processing
        if (output_file) {
          if (is_multi_read) {
            // Single multi-read file: treat output as directory
            snprintf(output_filename, sizeof(output_filename), "%s/read_ch%s_rd%u.txt",
                     output_file,
                     metadata[j].channel_number ? metadata[j].channel_number : "unknown",
                     metadata[j].read_number);
          } else {
            // Single single-read file: treat output as exact filename
            snprintf(output_filename, sizeof(output_filename), "%s", output_file);
          }
        } else {
          // No output specified: simple naming
          snprintf(output_filename, sizeof(output_filename), "read_ch%s_rd%u.txt",
                   metadata[j].channel_number ? metadata[j].channel_number : "unknown",
                   metadata[j].read_number);
        }
      } else {
        // Multiple file processing: always add filename prefix for traceability
        if (output_file) {
          // Multiple files to output directory: output_dir/originalfile_read_ch228_rd123.txt
          snprintf(output_filename, sizeof(output_filename), "%s/%.*s_read_ch%s_rd%u.txt",
                   output_file,
                   (int)(strstr(basename, ".fast5") - basename), basename,
                   metadata[j].channel_number ? metadata[j].channel_number : "unknown",
                   metadata[j].read_number);
        } else {
          // Multiple files to current directory: originalfile_read_ch228_rd123.txt
          snprintf(output_filename, sizeof(output_filename), "%.*s_read_ch%s_rd%u.txt",
                   (int)(strstr(basename, ".fast5") - basename), basename,
                   metadata[j].channel_number ? metadata[j].channel_number : "unknown",
                   metadata[j].read_number);
        }
      }
      
      // Write signal to file with metadata header
      if (write_signal_to_file(output_filename, signal, signal_length, &metadata[j]) == EXIT_SUCCESS) {
        if (verbose) {
          printf("  Wrote %zu samples to: %s\n", signal_length, output_filename);
        }
      }
      
      free_fast5_signal(signal);
      
      // Update read progress for single multi-read file
      if (show_read_progress) {
        display_progress_simple((int)(j + 1), (int)reads_to_process, verbose, "extracting reads");
      }
    }
    
    // Complete read progress bar for single multi-read file
    if (show_read_progress) {
      printf("\n");
    }
    
    free_fast5_metadata(metadata, metadata_count);
    
    // Update progress bar (only for multiple files)
    if (file_count > 1) {
      display_progress_simple((int)(i + 1), (int)file_count, verbose, "converting files");
    }
  }
  
  // Complete progress bar and move to next line (only for multiple files)
  if (file_count > 1) {
    printf("\n");
  }
  
  return EXIT_SUCCESS;
}

// **********************************************************************
// Metadata Extraction Functions
// **********************************************************************

int extract_metadata(char **files, size_t file_count, const char *output_file, bool verbose) {
  if (verbose) {
    printf("Converting %zu files to metadata format...\n", file_count);
  }
  
  FILE *output = stdout;
  if (output_file) {
    output = fopen(output_file, "w");
    if (!output) {
      warnx("Cannot create output file: %s", output_file);
      return EXIT_FAILURE;
    }
  }
  
  // Write header
  fprintf(output, "# Fast5 Metadata Export\n");
  fprintf(output, "# file_path\tread_id\tsignal_length\tsample_rate\tduration\tread_number\tis_multi_read\n");
  
  for (size_t i = 0; i < file_count; i++) {
    if (verbose) {
      printf("Processing file: %s\n", files[i]);
    }
    
    // Read metadata
    size_t metadata_count = 0;
    fast5_metadata_t *metadata = read_fast5_metadata_with_enhancer(files[i], &metadata_count, NULL);
    
    if (!metadata || metadata_count == 0) {
      warnx("Cannot read metadata from file: %s", files[i]);
      continue;
    }
    
    // Write metadata for each read
    for (size_t j = 0; j < metadata_count; j++) {
      fprintf(output, "%s\t%s\t%u\t%.0f\t%u\t%u\t%s\n",
              files[i],
              metadata[j].read_id ? metadata[j].read_id : "unknown",
              metadata[j].signal_length,
              metadata[j].sample_rate,
              metadata[j].duration,
              metadata[j].read_number,
              metadata[j].is_multi_read ? "true" : "false");
    }
    
    free_fast5_metadata(metadata, metadata_count);
  }
  
  if (output != stdout) {
    fclose(output);
  }
  
  return EXIT_SUCCESS;
}