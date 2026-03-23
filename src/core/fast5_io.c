// **********************************************************************
// core/fast5_io.c - Fast5 File I/O Operations for Sequelizer
// **********************************************************************
// Sebastian Claudiusz Magierowski Aug 3 2025
//
// Migrated from ciren Fast5 I/O operations including:
// - Fast5 file discovery and pattern matching  
// - Fast5 metadata extraction (single-read and multi-read formats)
// - Fast5 signal data extraction
// Used by sequelizer fast5 subcommand and future signal processing.

#include "fast5_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <hdf5.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <err.h>

// **********************************************************************
// Fast5 File Discovery and Validation Functions
// **********************************************************************

// Pattern matching for fast5 extension 
bool is_fast5_file(const char *filename) {
  if (!filename) return false;
  size_t len = strlen(filename);
  return len >= 6 && strcmp(filename + len - 6, ".fast5") == 0;
}

// Enhanced file validation functions (ported from ciren)
bool file_is_accessible(const char *filename) {
  struct stat file_stat;
  return (stat(filename, &file_stat) == 0 && S_ISREG(file_stat.st_mode));
}

bool is_likely_fast5_file(const char *filename) {
  if (!filename) return false;
  size_t len = strlen(filename);
  return len >= 6 && strcmp(filename + len - 6, ".fast5") == 0;
}

bool is_valid_hdf5_file(const char *filename) {
  // Suppress HDF5 error messages temporarily
  H5E_auto2_t old_func;
  void *old_client_data;
  H5Eget_auto2(H5E_DEFAULT, &old_func, &old_client_data);
  H5Eset_auto2(H5E_DEFAULT, NULL, NULL);
  
  // Simple HDF5 file validation
  hid_t file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
  bool is_valid = (file_id >= 0);
  
  if (is_valid) {
    H5Fclose(file_id);
  }
  
  // Restore HDF5 error reporting
  H5Eset_auto2(H5E_DEFAULT, old_func, old_client_data);
  
  return is_valid;
}

bool has_fast5_structure(const char *filename) {
  // Create thread-local error stack for thread safety
  hid_t error_stack = H5Ecreate_stack();
  if (error_stack >= 0) {
    H5Eset_auto2(error_stack, NULL, NULL);
  }
  
  hid_t file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
  if (file_id < 0) {
    if (error_stack >= 0) {
      H5Eclose_stack(error_stack);
    }
    return false;
  }
  
  bool has_structure = false;
  
  // Check for multi-read format indicators
  htri_t attr_exists = H5Aexists(file_id, "file_type");
  if (attr_exists > 0) {
    has_structure = true;
  } else {
    // Check for read_ groups at root level
    hsize_t num_objs;
    if (H5Gget_num_objs(file_id, &num_objs) >= 0 && num_objs > 0) {
      for (hsize_t i = 0; i < num_objs && i < 5; i++) {
        char obj_name[256];
        if (H5Gget_objname_by_idx(file_id, i, obj_name, sizeof(obj_name)) >= 0) {
          if (strncmp(obj_name, "read_", 5) == 0) {
            has_structure = true;
            break;
          }
        }
      }
    }
    
    // Check for single-read format
    if (!has_structure) {
      htri_t exists = H5Lexists(file_id, "/Raw/Reads", H5P_DEFAULT);
      if (exists > 0) {
        has_structure = true;
      }
    }
  }
  
  H5Fclose(file_id);
  
  // Clean up thread-local error stack
  if (error_stack >= 0) {
    H5Eclose_stack(error_stack);
  }
  
  return has_structure;
}

// Recursive directory traversal
char** find_fast5_files_recursive(const char *directory, size_t *count) {
  DIR *dir;
  struct dirent *entry;
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
    if (entry->d_name[0] == '.') continue; // Skip hidden files and . ..
    
    snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);
    
    if (stat(path, &file_stat) != 0) {
      warnx("Cannot stat file: %s", path);
      continue;
    }
    
    if (S_ISDIR(file_stat.st_mode)) {
      // Recursively search subdirectories
      size_t subdir_count;
      char **subdir_files = find_fast5_files_recursive(path, &subdir_count);
      
      if (subdir_files && subdir_count > 0) {
        // Ensure capacity is sufficient for new files
        size_t new_total = *count + subdir_count;
        if (new_total > files_capacity) {
          files_capacity = new_total;
          files = realloc(files, files_capacity * sizeof(char*));
          if (!files) {
            errx(EXIT_FAILURE, "Memory allocation failed");
          }
        }
        
        // Copy subdirectory files to main array
        for (size_t i = 0; i < subdir_count; i++) {
          files[*count + i] = subdir_files[i];
        }
        *count += subdir_count;
        free(subdir_files); // Free the array, but not the strings
      }
    } else if (S_ISREG(file_stat.st_mode) && is_fast5_file(entry->d_name)) {
      // Add Fast5 file to list
      if (*count >= files_capacity) {
        // Start with 1024 instead of 16 to reduce realloc() calls for large directories
        // Typical nanopore runs have thousands of files, so this avoids multiple reallocations
        files_capacity = files_capacity == 0 ? 1024 : files_capacity * 2;
        files = realloc(files, files_capacity * sizeof(char*));
        if (!files) {
          errx(EXIT_FAILURE, "Memory allocation failed");
        }
      }
      
      files[*count] = strdup(path);
      if (!files[*count]) {
        errx(EXIT_FAILURE, "Memory allocation failed");
      }
      (*count)++;

      // Show progress every 500 files to give feedback during slow directory scans
      if (*count % 500 == 0) {
        printf("\rDiscovered %zu Fast5 files...", *count);
        fflush(stdout);
      }
    }
  }

  closedir(dir);

  // Clear the progress line if we showed any updates
  if (*count >= 500) {
    printf("\r");
    fflush(stdout);
  }

  return files;
}

// Main discovery function handling files/directories
char** find_fast5_files(const char *input_path, bool recursive, size_t *count) {
  struct stat path_stat;
  *count = 0;
  
  // Check if input path exists
  if (stat(input_path, &path_stat) != 0) {
    errx(EXIT_FAILURE, "Input path does not exist: %s", input_path);
  }
  
  if (S_ISREG(path_stat.st_mode)) {
    // Single file
    if (is_fast5_file(input_path)) {
      char **files = malloc(sizeof(char*));
      if (!files) {
        errx(EXIT_FAILURE, "Memory allocation failed");
      }
      files[0] = strdup(input_path);
      if (!files[0]) {
        errx(EXIT_FAILURE, "Memory allocation failed");
      }
      *count = 1;
      return files;
    } else {
      errx(EXIT_FAILURE, "Input file is not a Fast5 file: %s", input_path);
    }
  } else if (S_ISDIR(path_stat.st_mode)) {
    // Directory
    if (recursive) {
      return find_fast5_files_recursive(input_path, count);
    } else {
      // Non-recursive directory search
      DIR *dir;
      struct dirent *entry;
      char **files = NULL;
      size_t files_capacity = 0;

      dir = opendir(input_path);
      if (!dir) {
        errx(EXIT_FAILURE, "Cannot open directory: %s", input_path);
      }

      while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue; // Skip hidden files

        if (is_fast5_file(entry->d_name)) {
          if (*count >= files_capacity) {
            // Start with 1024 instead of 16 to reduce realloc() calls for large directories
            // Typical nanopore runs have thousands of files, so this avoids multiple reallocations
            files_capacity = files_capacity == 0 ? 1024 : files_capacity * 2;
            files = realloc(files, files_capacity * sizeof(char*));
            if (!files) {
              errx(EXIT_FAILURE, "Memory allocation failed");
            }
          }
          
          char path[PATH_MAX];
          snprintf(path, sizeof(path), "%s/%s", input_path, entry->d_name);
          files[*count] = strdup(path);
          if (!files[*count]) {
            errx(EXIT_FAILURE, "Memory allocation failed");
          }
          (*count)++;

          // Show progress every 500 files to give feedback during slow directory scans
          if (*count % 500 == 0) {
            printf("\rDiscovered %zu Fast5 files...", *count);
            fflush(stdout);
          }
        }
      }

      closedir(dir);

      // Clear the progress line if we showed any updates
      if (*count >= 500) {
        printf("\r");
        fflush(stdout);
      }

      return files;
    }
  } else {
    errx(EXIT_FAILURE, "Input path is neither a file nor a directory: %s", input_path);
  }
  
  return NULL;
}

// Memory cleanup
void free_file_list(char **files, size_t count) {
  if (files) {
    for (size_t i = 0; i < count; i++) {
      free(files[i]);
    }
    free(files);
  }
}

// **********************************************************************
// Fast5 File Reading Functions - Helper Functions
// **********************************************************************

// Helper function to read string attribute
static char* read_string_attribute(hid_t obj_id, const char *attr_name) {
  hid_t attr_id = H5Aopen(obj_id, attr_name, H5P_DEFAULT);
  if (attr_id < 0) return NULL;
  
  hid_t type_id = H5Aget_type(attr_id);
  char *buffer = NULL;

  if (H5Tis_variable_str(type_id) > 0) {
    char *vlen_buffer = NULL;
    if (H5Aread(attr_id, type_id, &vlen_buffer) >= 0 && vlen_buffer) {
      buffer = strdup(vlen_buffer);
      H5free_memory(vlen_buffer);
    }
  } else {
    size_t size = H5Tget_size(type_id);
    buffer = malloc(size + 1);
    if (buffer) {
      if (H5Aread(attr_id, type_id, buffer) < 0) {
        free(buffer);
        buffer = NULL;
      } else {
        buffer[size] = '\0';
      }
    }
  }
  
  H5Tclose(type_id);
  H5Aclose(attr_id);
  return buffer;
}

static bool detect_fast5_is_multi_read(hid_t file_id) {
  char *file_type = read_string_attribute(file_id, "file_type");
  if (file_type) {
    bool is_multi = strcmp(file_type, "multi-read") == 0;
    free(file_type);
    return is_multi;
  }

  hsize_t num_objs;
  if (H5Gget_num_objs(file_id, &num_objs) >= 0 && num_objs > 0) {
    for (hsize_t i = 0; i < num_objs && i < 5; i++) {
      char obj_name[256];
      if (H5Gget_objname_by_idx(file_id, i, obj_name, sizeof(obj_name)) >= 0) {
        if (strncmp(obj_name, "read_", 5) == 0) {
          return true;
        }
      }
    }
  }

  return false;
}

// Helper function to read uint32 attribute
static bool read_uint32_attribute(hid_t obj_id, const char *attr_name, uint32_t *value) {
  hid_t attr_id = H5Aopen(obj_id, attr_name, H5P_DEFAULT);
  if (attr_id < 0) return false;
  
  bool success = (H5Aread(attr_id, H5T_NATIVE_UINT32, value) >= 0);
  H5Aclose(attr_id);
  return success;
}

// Helper function to read double attribute
static bool read_double_attribute(hid_t obj_id, const char *attr_name, double *value) {
  hid_t attr_id = H5Aopen(obj_id, attr_name, H5P_DEFAULT);
  if (attr_id < 0) return false;
  
  bool success = (H5Aread(attr_id, H5T_NATIVE_DOUBLE, value) >= 0);
  H5Aclose(attr_id);
  return success;
}

// Helper function to get signal dataset dimensions
static hsize_t get_signal_length(hid_t signal_dataset_id) {
  hid_t space_id = H5Dget_space(signal_dataset_id);
  if (space_id < 0) return 0;
  
  hsize_t dims[1];
  int ndims = H5Sget_simple_extent_dims(space_id, dims, NULL);
  H5Sclose(space_id);
  
  return (ndims == 1) ? dims[0] : 0;
}

// **********************************************************************
// Fast5 Metadata Reading Functions
// **********************************************************************

// Free Fast5 metadata
void free_fast5_metadata(fast5_metadata_t *metadata, size_t count) {
  if (!metadata) return;
  
  for (size_t i = 0; i < count; i++) {
    free(metadata[i].read_id);
    free(metadata[i].file_path);
    // Only free if it exists (safe for both basic and enhanced)
    free(metadata[i].compression_method);
    free(metadata[i].run_id);
    free(metadata[i].channel_number);
  }
  free(metadata);
}

// **********************************************************************
// Metadata Enhancer Functions
// **********************************************************************

// Helper function to validate that a string contains only printable ASCII characters
static bool is_valid_text_string(const char *str, size_t max_len) {
  if (!str) return false;

  for (size_t i = 0; i < max_len && str[i] != '\0'; i++) {
    // Check if character is printable ASCII (space to tilde: 0x20-0x7E) or tab/newline
    if (str[i] != '\t' && str[i] != '\n' && (str[i] < 0x20 || str[i] > 0x7E)) {
      return false;
    }
  }
  return true;
}

// Enhancer function to extract tracking_id metadata (run_id, etc.)
void extract_tracking_id(hid_t file_id, hid_t signal_dataset_id, fast5_metadata_t *metadata) {
  // Initialize field
  metadata->run_id = NULL;

  if (signal_dataset_id < 0) return;

  // Suppress HDF5 error messages for missing groups/attributes
  H5E_auto2_t old_func;
  void *old_client_data;
  H5Eget_auto2(H5E_DEFAULT, &old_func, &old_client_data);
  H5Eset_auto2(H5E_DEFAULT, NULL, NULL);

  // Get the dataset path to determine format
  char obj_name[256];
  ssize_t name_len = H5Iget_name(signal_dataset_id, obj_name, sizeof(obj_name));
  if (name_len <= 0) {
    H5Eset_auto2(H5E_DEFAULT, old_func, old_client_data);
    return;
  }

  // Parse path to determine format and extract run_id
  if (strstr(obj_name, "/Raw/Reads/")) {
    // Single-read format: /Raw/Reads/Read_xxx/Signal
    // Get run_id from /UniqueGlobalKey/tracking_id/run_id
    hid_t tracking_group_id = H5Gopen2(file_id, "/UniqueGlobalKey/tracking_id", H5P_DEFAULT);
    if (tracking_group_id >= 0) {
      hid_t attr_id = H5Aopen(tracking_group_id, "run_id", H5P_DEFAULT);
      if (attr_id >= 0) {
        hid_t type_id = H5Aget_type(attr_id);
        htri_t is_vlen = H5Tis_variable_str(type_id);

        if (is_vlen > 0) {
          // Variable-length string: read pointer to string
          char *vlen_str = NULL;
          herr_t status = H5Aread(attr_id, type_id, &vlen_str);
          if (status >= 0 && vlen_str) {
            // Validate the string
            size_t len = strlen(vlen_str);
            if (is_valid_text_string(vlen_str, len)) {
              metadata->run_id = strdup(vlen_str);
            }
            // Free HDF5's memory
            H5free_memory(vlen_str);
          }
        } else {
          // Fixed-length string: read into buffer
          size_t size = H5Tget_size(type_id);
          char *temp_str = malloc(size + 1);
          if (temp_str) {
            memset(temp_str, 0, size + 1);
            H5Aread(attr_id, type_id, temp_str);
            temp_str[size] = '\0';

            // Validate that the string contains printable characters
            if (is_valid_text_string(temp_str, size)) {
              metadata->run_id = temp_str;
            } else {
              free(temp_str);
              metadata->run_id = NULL;
            }
          }
        }
        H5Tclose(type_id);
        H5Aclose(attr_id);
      }
      H5Gclose(tracking_group_id);
    }
  } else if (strstr(obj_name, "/read_")) {
    // Multi-read format: /read_xxx/Raw/Signal
    // Find the read group (extract read_xxx path)
    char read_path[256];
    strncpy(read_path, obj_name, sizeof(read_path));
    char *raw_pos = strstr(read_path, "/Raw/Signal");
    if (raw_pos) {
      *raw_pos = '\0'; // Get read_xxx path

      // Get run_id from read_xxx/tracking_id/run_id
      char tracking_path[512];
      snprintf(tracking_path, sizeof(tracking_path), "%s/tracking_id", read_path);
      hid_t tracking_group_id = H5Gopen2(file_id, tracking_path, H5P_DEFAULT);
      if (tracking_group_id >= 0) {
        hid_t attr_id = H5Aopen(tracking_group_id, "run_id", H5P_DEFAULT);
        if (attr_id >= 0) {
          hid_t type_id = H5Aget_type(attr_id);
          htri_t is_vlen = H5Tis_variable_str(type_id);

          if (is_vlen > 0) {
            // Variable-length string: read pointer to string
            char *vlen_str = NULL;
            herr_t status = H5Aread(attr_id, type_id, &vlen_str);
            if (status >= 0 && vlen_str) {
              // Validate the string
              size_t len = strlen(vlen_str);
              if (is_valid_text_string(vlen_str, len)) {
                metadata->run_id = strdup(vlen_str);
              }
              // Free HDF5's memory
              H5free_memory(vlen_str);
            }
          } else {
            // Fixed-length string: read into buffer
            size_t size = H5Tget_size(type_id);
            char *temp_str = malloc(size + 1);
            if (temp_str) {
              memset(temp_str, 0, size + 1);
              H5Aread(attr_id, type_id, temp_str);
              temp_str[size] = '\0';

              // Validate that the string contains printable characters
              if (is_valid_text_string(temp_str, size)) {
                metadata->run_id = temp_str;
              } else {
                free(temp_str);
                metadata->run_id = NULL;
              }
            }
          }
          H5Tclose(type_id);
          H5Aclose(attr_id);
        }
        H5Gclose(tracking_group_id);
      }
    }
  }

  // Restore HDF5 error reporting
  H5Eset_auto2(H5E_DEFAULT, old_func, old_client_data);
}

// Enhancer function to extract channel_number from channel_id group
void extract_channel_id(hid_t file_id, hid_t signal_dataset_id, fast5_metadata_t *metadata) {
  // Initialize field
  metadata->channel_number = NULL;

  if (signal_dataset_id < 0) return;

  // Suppress HDF5 error messages for missing groups/attributes
  H5E_auto2_t old_func;
  void *old_client_data;
  H5Eget_auto2(H5E_DEFAULT, &old_func, &old_client_data);
  H5Eset_auto2(H5E_DEFAULT, NULL, NULL);

  // Get the dataset path to determine format
  char obj_name[256];
  ssize_t name_len = H5Iget_name(signal_dataset_id, obj_name, sizeof(obj_name));
  if (name_len <= 0) {
    H5Eset_auto2(H5E_DEFAULT, old_func, old_client_data);
    return;
  }

  // Parse path to determine format and extract channel_number
  if (strstr(obj_name, "/Raw/Reads/")) {
    // Single-read format: /Raw/Reads/Read_xxx/Signal
    // Get channel_number from /UniqueGlobalKey/channel_id/channel_number
    hid_t channel_group_id = H5Gopen2(file_id, "/UniqueGlobalKey/channel_id", H5P_DEFAULT);
    if (channel_group_id >= 0) {
      hid_t attr_id = H5Aopen(channel_group_id, "channel_number", H5P_DEFAULT);
      if (attr_id >= 0) {
        hid_t type_id = H5Aget_type(attr_id);
        htri_t is_vlen = H5Tis_variable_str(type_id);

        if (is_vlen > 0) {
          // Variable-length string: read pointer to string
          char *vlen_str = NULL;
          herr_t status = H5Aread(attr_id, type_id, &vlen_str);
          if (status >= 0 && vlen_str) {
            // Validate the string
            size_t len = strlen(vlen_str);
            if (is_valid_text_string(vlen_str, len)) {
              metadata->channel_number = strdup(vlen_str);
            }
            // Free HDF5's memory
            H5free_memory(vlen_str);
          }
        } else {
          // Fixed-length string: read into buffer
          size_t size = H5Tget_size(type_id);
          char *temp_str = malloc(size + 1);
          if (temp_str) {
            memset(temp_str, 0, size + 1);
            H5Aread(attr_id, type_id, temp_str);
            temp_str[size] = '\0';

            // Validate that the string contains printable characters
            if (is_valid_text_string(temp_str, size)) {
              metadata->channel_number = temp_str;
            } else {
              free(temp_str);
              metadata->channel_number = NULL;
            }
          }
        }
        H5Tclose(type_id);
        H5Aclose(attr_id);
      }
      H5Gclose(channel_group_id);
    }
  } else if (strstr(obj_name, "/read_")) {
    // Multi-read format: /read_xxx/Raw/Signal
    // Find the read group (extract read_xxx path)
    char read_path[256];
    strncpy(read_path, obj_name, sizeof(read_path));
    char *raw_pos = strstr(read_path, "/Raw/Signal");
    if (raw_pos) {
      *raw_pos = '\0'; // Get read_xxx path

      // Get channel_number from read_xxx/channel_id/channel_number
      char channel_path[512];
      snprintf(channel_path, sizeof(channel_path), "%s/channel_id", read_path);
      hid_t channel_group_id = H5Gopen2(file_id, channel_path, H5P_DEFAULT);
      if (channel_group_id >= 0) {
        hid_t attr_id = H5Aopen(channel_group_id, "channel_number", H5P_DEFAULT);
        if (attr_id >= 0) {
          hid_t type_id = H5Aget_type(attr_id);
          htri_t is_vlen = H5Tis_variable_str(type_id);

          if (is_vlen > 0) {
            // Variable-length string: read pointer to string
            char *vlen_str = NULL;
            herr_t status = H5Aread(attr_id, type_id, &vlen_str);
            if (status >= 0 && vlen_str) {
              // Validate the string
              size_t len = strlen(vlen_str);
              if (is_valid_text_string(vlen_str, len)) {
                metadata->channel_number = strdup(vlen_str);
              }
              // Free HDF5's memory
              H5free_memory(vlen_str);
            }
          } else {
            // Fixed-length string: read into buffer
            size_t size = H5Tget_size(type_id);
            char *temp_str = malloc(size + 1);
            if (temp_str) {
              memset(temp_str, 0, size + 1);
              H5Aread(attr_id, type_id, temp_str);
              temp_str[size] = '\0';

              // Validate that the string contains printable characters
              if (is_valid_text_string(temp_str, size)) {
                metadata->channel_number = temp_str;
              } else {
                free(temp_str);
                metadata->channel_number = NULL;
              }
            }
          }
          H5Tclose(type_id);
          H5Aclose(attr_id);
        }
        H5Gclose(channel_group_id);
      }
    }
  }

  // Restore HDF5 error reporting
  H5Eset_auto2(H5E_DEFAULT, old_func, old_client_data);
}

// Enhancer function to extract calibration parameters from Fast5 files
void extract_calibration_parameters(hid_t file_id, hid_t signal_dataset_id, fast5_metadata_t *metadata) {
  // Initialize calibration fields
  metadata->offset = 0.0;
  metadata->range = 0.0;
  metadata->digitisation = 0.0;
  metadata->calibration_available = false;

  if (signal_dataset_id < 0) return;

  // Suppress HDF5 error messages for missing groups/attributes
  H5E_auto2_t old_func;
  void *old_client_data;
  H5Eget_auto2(H5E_DEFAULT, &old_func, &old_client_data);
  H5Eset_auto2(H5E_DEFAULT, NULL, NULL);
  
  // Get the dataset path to determine read location
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
    bool got_offset = false, got_range = false, got_digitisation = false;
    
    // Extract offset
    hid_t offset_attr = H5Aopen(channel_group_id, "offset", H5P_DEFAULT);
    if (offset_attr >= 0) {
      if (H5Aread(offset_attr, H5T_NATIVE_DOUBLE, &metadata->offset) >= 0) {
        got_offset = true;
      }
      H5Aclose(offset_attr);
    }
    
    // Extract range
    hid_t range_attr = H5Aopen(channel_group_id, "range", H5P_DEFAULT);
    if (range_attr >= 0) {
      if (H5Aread(range_attr, H5T_NATIVE_DOUBLE, &metadata->range) >= 0) {
        got_range = true;
      }
      H5Aclose(range_attr);
    }
    
    // Extract digitisation
    hid_t digitisation_attr = H5Aopen(channel_group_id, "digitisation", H5P_DEFAULT);
    if (digitisation_attr >= 0) {
      if (H5Aread(digitisation_attr, H5T_NATIVE_DOUBLE, &metadata->digitisation) >= 0) {
        got_digitisation = true;
      }
      H5Aclose(digitisation_attr);
    }
    
    // Mark calibration as available if we got all three parameters
    metadata->calibration_available = got_offset && got_range && got_digitisation;

    H5Gclose(channel_group_id);
  }

  // Restore HDF5 error reporting
  H5Eset_auto2(H5E_DEFAULT, old_func, old_client_data);
}

// Enhancer function to extract raw signal metadata (median_before and start_time)
void extract_raw(hid_t file_id, hid_t signal_dataset_id, fast5_metadata_t *metadata) {
  // Initialize fields
  metadata->median_before = 0.0;
  metadata->pore_level_available = false;
  metadata->start_time = 0;

  if (signal_dataset_id < 0) return;

  // Suppress HDF5 error messages
  H5E_auto2_t old_func;
  void *old_client_data;
  H5Eget_auto2(H5E_DEFAULT, &old_func, &old_client_data);
  H5Eset_auto2(H5E_DEFAULT, NULL, NULL);

  // Get the parent group of the signal dataset (the Raw group)
  char obj_name[256];
  ssize_t name_len = H5Iget_name(signal_dataset_id, obj_name, sizeof(obj_name));
  if (name_len <= 0) {
    H5Eset_auto2(H5E_DEFAULT, old_func, old_client_data);
    return;
  }

  // Parse the path to find the Raw group
  // For single-read: /Raw/Reads/Read_X/Signal -> /Raw/Reads/Read_X/
  // For multi-read: /read_X/Raw/Signal -> /read_X/Raw/
  char *last_slash = strrchr(obj_name, '/');
  if (!last_slash) {
    H5Eset_auto2(H5E_DEFAULT, old_func, old_client_data);
    return;
  }

  // Null terminate at last slash to get parent group path
  *last_slash = '\0';

  // Open the Raw group (parent of Signal dataset)
  hid_t raw_group_id = H5Gopen2(file_id, obj_name, H5P_DEFAULT);
  if (raw_group_id >= 0) {
    // Try to read median_before attribute
    hid_t median_attr = H5Aopen(raw_group_id, "median_before", H5P_DEFAULT);
    if (median_attr >= 0) {
      if (H5Aread(median_attr, H5T_NATIVE_DOUBLE, &metadata->median_before) >= 0) {
        metadata->pore_level_available = true;
      }
      H5Aclose(median_attr);
    }

    // Try to read start_time attribute
    hid_t time_attr = H5Aopen(raw_group_id, "start_time", H5P_DEFAULT);
    if (time_attr >= 0) {
      H5Aread(time_attr, H5T_NATIVE_UINT64, &metadata->start_time);
      H5Aclose(time_attr);
    }

    H5Gclose(raw_group_id);
  }

  // Restore HDF5 error reporting
  H5Eset_auto2(H5E_DEFAULT, old_func, old_client_data);
}

// **********************************************************************
// Fast5 Metadata Reading Functions (ENHANCED)
// **********************************************************************
// Enhanced single-read metadata function with enhancer support
static fast5_metadata_t* read_single_read_metadata_with_enhancer(hid_t file_id, const char *filename, size_t *count, metadata_enhancer_t enhancer) {
  *count = 0;
  
  // Suppress HDF5 error messages temporarily
  H5E_auto2_t old_func;
  void *old_client_data;
  H5Eget_auto2(H5E_DEFAULT, &old_func, &old_client_data);
  H5Eset_auto2(H5E_DEFAULT, NULL, NULL);
  
  // Check if /Raw/Reads group exists
  htri_t exists = H5Lexists(file_id, "/Raw/Reads", H5P_DEFAULT);
  
  // Restore HDF5 error reporting
  H5Eset_auto2(H5E_DEFAULT, old_func, old_client_data);
  
  if (exists <= 0) return NULL;
  
  hid_t reads_group_id = H5Gopen2(file_id, "/Raw/Reads", H5P_DEFAULT);
  if (reads_group_id < 0) return NULL;
  
  // Get number of reads in the group
  hsize_t num_objs;
  if (H5Gget_num_objs(reads_group_id, &num_objs) < 0) {
    H5Gclose(reads_group_id);
    return NULL;
  }
  
  if (num_objs == 0) {
    H5Gclose(reads_group_id);
    return NULL;
  }
  
  fast5_metadata_t *metadata = calloc(num_objs, sizeof(fast5_metadata_t)); // allocate space for file metadata
  if (!metadata) {
    H5Gclose(reads_group_id);
    return NULL;
  }
  
  // Process each read
  for (hsize_t i = 0; i < num_objs; i++) {
    char read_name[256];
    if (H5Gget_objname_by_idx(reads_group_id, i, read_name, sizeof(read_name)) < 0) continue;
    
    char read_path[512];
    snprintf(read_path, sizeof(read_path), "/Raw/Reads/%s", read_name);
    
    hid_t read_group_id = H5Gopen2(file_id, read_path, H5P_DEFAULT);
    if (read_group_id < 0) continue;
    
    // Initialize metadata
    metadata[*count].file_path = strdup(filename);
    metadata[*count].is_multi_read = false;
    
    // Read attributes
    metadata[*count].read_id = read_string_attribute(read_group_id, "read_id");
    read_uint32_attribute(read_group_id, "duration", &metadata[*count].duration);
    read_uint32_attribute(read_group_id, "read_number", &metadata[*count].read_number);
    
    // Get signal length from Signal dataset
    hid_t signal_dataset_id = H5Dopen2(read_group_id, "Signal", H5P_DEFAULT);
    if (signal_dataset_id >= 0) {
      metadata[*count].signal_length = (uint32_t)get_signal_length(signal_dataset_id);
      
      // *** CALL ENHANCER HERE - while signal dataset is open ***
      if (enhancer) {
        enhancer(file_id, signal_dataset_id, &metadata[*count]);
      }
      
      H5Dclose(signal_dataset_id);
    }
    
    H5Gclose(read_group_id);
    (*count)++;
  }
  
  // Get sample rate from channel_id group
  hid_t channel_group_id = H5Gopen2(file_id, "/UniqueGlobalKey/channel_id", H5P_DEFAULT);
  if (channel_group_id >= 0) {
    double sample_rate;
    if (read_double_attribute(channel_group_id, "sampling_rate", &sample_rate)) {
      for (size_t i = 0; i < *count; i++) {
        metadata[i].sample_rate = sample_rate;
      }
    }
    H5Gclose(channel_group_id);
  }
  
  H5Gclose(reads_group_id);
  return metadata;
}

// Enhanced multi-read metadata function with enhancer support
static fast5_metadata_t* read_multi_read_metadata_with_enhancer(hid_t file_id, const char *filename, size_t *count, metadata_enhancer_t enhancer) {
  *count = 0;
  
  // Get number of root-level objects
  hsize_t num_objs;
  if (H5Gget_num_objs(file_id, &num_objs) < 0) return NULL;
  
  // Count read_ groups
  size_t read_count = 0;
  for (hsize_t i = 0; i < num_objs; i++) {
    char obj_name[256];
    if (H5Gget_objname_by_idx(file_id, i, obj_name, sizeof(obj_name)) < 0) continue;
    
    if (strncmp(obj_name, "read_", 5) == 0) {
      read_count++;
    }
  }
  
  if (read_count == 0) return NULL;
  
  fast5_metadata_t *metadata = calloc(read_count, sizeof(fast5_metadata_t));
  if (!metadata) return NULL;
  
  // Process each read_ group
  for (hsize_t i = 0; i < num_objs; i++) {
    char obj_name[256];
    if (H5Gget_objname_by_idx(file_id, i, obj_name, sizeof(obj_name)) < 0) continue;
    
    if (strncmp(obj_name, "read_", 5) != 0) continue;
    
    hid_t read_group_id = H5Gopen2(file_id, obj_name, H5P_DEFAULT);
    if (read_group_id < 0) continue;
    
    // Open Raw subgroup
    hid_t raw_group_id = H5Gopen2(read_group_id, "Raw", H5P_DEFAULT);
    if (raw_group_id < 0) {
      H5Gclose(read_group_id);
      continue;
    }
    
    // Initialize metadata
    metadata[*count].file_path = strdup(filename);
    metadata[*count].is_multi_read = true;
    
    // Read attributes from Raw group
    metadata[*count].read_id = read_string_attribute(raw_group_id, "read_id");
    read_uint32_attribute(raw_group_id, "duration", &metadata[*count].duration);
    read_uint32_attribute(raw_group_id, "read_number", &metadata[*count].read_number);
    
    // Get signal length from Signal dataset
    hid_t signal_dataset_id = H5Dopen2(raw_group_id, "Signal", H5P_DEFAULT);
    if (signal_dataset_id >= 0) {
      metadata[*count].signal_length = (uint32_t)get_signal_length(signal_dataset_id);
      
      // *** CALL ENHANCER HERE - while signal dataset is open ***
      if (enhancer) {
        enhancer(file_id, signal_dataset_id, &metadata[*count]);
      }
      
      H5Dclose(signal_dataset_id);
    }
    
    // Get sample rate from channel_id group
    hid_t channel_group_id = H5Gopen2(read_group_id, "channel_id", H5P_DEFAULT);
    if (channel_group_id >= 0) {
      read_double_attribute(channel_group_id, "sampling_rate", &metadata[*count].sample_rate);
      H5Gclose(channel_group_id);
    }
    
    H5Gclose(raw_group_id);
    H5Gclose(read_group_id);
    (*count)++;
  }
  
  return metadata;
}

// Main function to read Fast5 metadata with enhancer support
fast5_metadata_t* read_fast5_metadata_with_enhancer(const char *filename, size_t *metadata_count, metadata_enhancer_t enhancer) {
  if (!filename || !metadata_count) return NULL;
  
  *metadata_count = 0;
  
  // Suppress HDF5 error messages temporarily
  H5E_auto2_t old_func;
  void *old_client_data;
  H5Eget_auto2(H5E_DEFAULT, &old_func, &old_client_data);
  H5Eset_auto2(H5E_DEFAULT, NULL, NULL);
  
  // Open the Fast5 file
  hid_t file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
  
  // Restore HDF5 error reporting
  H5Eset_auto2(H5E_DEFAULT, old_func, old_client_data);
  
  if (file_id < 0) {
    warnx("Failed to open Fast5 file: %s", filename);
    return NULL;
  }
  
  fast5_metadata_t *metadata = NULL;
  
  bool is_multi_read = detect_fast5_is_multi_read(file_id);
  
  // Call the appropriate enhanced helper function
  if (is_multi_read) {
    metadata = read_multi_read_metadata_with_enhancer(file_id, filename, metadata_count, enhancer);
  } else {
    metadata = read_single_read_metadata_with_enhancer(file_id, filename, metadata_count, enhancer);
  }
  
  H5Fclose(file_id);
  return metadata;
}

// **********************************************************************
// Fast5 Signal Extraction Functions
// **********************************************************************
// Extract signal data from single-read Fast5 format
static float* read_single_read_signal(hid_t file_id, const char *read_id, size_t *signal_length) {
  *signal_length = 0;
  
  // Suppress HDF5 error messages temporarily
  H5E_auto2_t old_func;
  void *old_client_data;
  H5Eget_auto2(H5E_DEFAULT, &old_func, &old_client_data);
  H5Eset_auto2(H5E_DEFAULT, NULL, NULL);
  
  // Check if /Raw/Reads group exists
  htri_t exists = H5Lexists(file_id, "/Raw/Reads", H5P_DEFAULT);
  
  // Restore HDF5 error reporting
  H5Eset_auto2(H5E_DEFAULT, old_func, old_client_data);
  
  if (exists <= 0) return NULL;
  
  hid_t reads_group_id = H5Gopen2(file_id, "/Raw/Reads", H5P_DEFAULT);
  if (reads_group_id < 0) return NULL;
  
  // Get number of reads in the group
  hsize_t num_objs;
  if (H5Gget_num_objs(reads_group_id, &num_objs) < 0) {
    H5Gclose(reads_group_id);
    return NULL;
  }
  
  float *signal = NULL;
  
  // Search for the read
  for (hsize_t i = 0; i < num_objs; i++) {
    char read_name[256];
    if (H5Gget_objname_by_idx(reads_group_id, i, read_name, sizeof(read_name)) < 0) continue;
    
    char read_path[512];
    snprintf(read_path, sizeof(read_path), "/Raw/Reads/%s", read_name);
    
    hid_t read_group_id = H5Gopen2(file_id, read_path, H5P_DEFAULT);
    if (read_group_id < 0) continue;
    
    // Check if this is the read we want (if read_id is specified)
    if (read_id) {
      char *current_read_id = read_string_attribute(read_group_id, "read_id");
      if (!current_read_id || strcmp(current_read_id, read_id) != 0) {
        free(current_read_id);
        H5Gclose(read_group_id);
        continue;
      }
      free(current_read_id);
    }
    
    // Open Signal dataset
    hid_t signal_dataset_id = H5Dopen2(read_group_id, "Signal", H5P_DEFAULT);
    if (signal_dataset_id >= 0) {
      hsize_t dims[1];
      hid_t space_id = H5Dget_space(signal_dataset_id);
      int ndims = H5Sget_simple_extent_dims(space_id, dims, NULL);
      H5Sclose(space_id);
      
      if (ndims == 1 && dims[0] > 0) {
        *signal_length = dims[0];
        signal = malloc(dims[0] * sizeof(float));
        if (signal) {
          if (H5Dread(signal_dataset_id, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, signal) < 0) {
            free(signal);
            signal = NULL;
            *signal_length = 0;
          }
        }
      }
      H5Dclose(signal_dataset_id);
    }
    
    H5Gclose(read_group_id);
    
    // If we found a signal, break (unless read_id was NULL, in which case we take the first one)
    if (signal) break;
  }
  
  H5Gclose(reads_group_id);
  return signal;
}

// Extract signal data from multi-read Fast5 format
static float* read_multi_read_signal(hid_t file_id, const char *read_id, size_t *signal_length) {
  *signal_length = 0;
  
  // Get number of root-level objects
  hsize_t num_objs;
  if (H5Gget_num_objs(file_id, &num_objs) < 0) return NULL;
  
  float *signal = NULL;
  
  // Process each read_ group
  for (hsize_t i = 0; i < num_objs; i++) {
    char obj_name[256];
    if (H5Gget_objname_by_idx(file_id, i, obj_name, sizeof(obj_name)) < 0) continue;
    
    if (strncmp(obj_name, "read_", 5) != 0) continue;
    
    hid_t read_group_id = H5Gopen2(file_id, obj_name, H5P_DEFAULT);
    if (read_group_id < 0) continue;
    
    // Open Raw subgroup
    hid_t raw_group_id = H5Gopen2(read_group_id, "Raw", H5P_DEFAULT);
    if (raw_group_id < 0) {
      H5Gclose(read_group_id);
      continue;
    }
    
    // Check if this is the read we want (if read_id is specified)
    if (read_id) {
      char *current_read_id = read_string_attribute(raw_group_id, "read_id");
      if (!current_read_id || strcmp(current_read_id, read_id) != 0) {
        free(current_read_id);
        H5Gclose(raw_group_id);
        H5Gclose(read_group_id);
        continue;
      }
      free(current_read_id);
    }
    
    // Open Signal dataset
    hid_t signal_dataset_id = H5Dopen2(raw_group_id, "Signal", H5P_DEFAULT);
    if (signal_dataset_id >= 0) {
      hsize_t dims[1];
      hid_t space_id = H5Dget_space(signal_dataset_id);
      int ndims = H5Sget_simple_extent_dims(space_id, dims, NULL);
      H5Sclose(space_id);
      
      if (ndims == 1 && dims[0] > 0) {
        *signal_length = dims[0];
        signal = malloc(dims[0] * sizeof(float));
        if (signal) {
          if (H5Dread(signal_dataset_id, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, signal) < 0) {
            free(signal);
            signal = NULL;
            *signal_length = 0;
          }
        }
      }
      H5Dclose(signal_dataset_id);
    }
    
    H5Gclose(raw_group_id);
    H5Gclose(read_group_id);
    
    // If we found a signal, break (unless read_id was NULL, in which case we take the first one)
    if (signal) break;
  }
  
  return signal;
}

// Main function to read Fast5 signal data
float* read_fast5_signal(const char *filename, const char *read_id, size_t *signal_length) {
  if (!filename || !signal_length) return NULL;
  
  *signal_length = 0;
  
  // Suppress HDF5 error messages temporarily
  H5E_auto2_t old_func;
  void *old_client_data;
  H5Eget_auto2(H5E_DEFAULT, &old_func, &old_client_data);
  H5Eset_auto2(H5E_DEFAULT, NULL, NULL);
  
  // Open the Fast5 file
  hid_t file_id = H5Fopen(filename, H5F_ACC_RDONLY, H5P_DEFAULT);
  
  // Restore HDF5 error reporting
  H5Eset_auto2(H5E_DEFAULT, old_func, old_client_data);
  
  if (file_id < 0) {
    warnx("Failed to open Fast5 file: %s", filename);
    return NULL;
  }
  
  float *signal = NULL;
  
  bool is_multi_read = detect_fast5_is_multi_read(file_id);
  
  if (is_multi_read) {
    signal = read_multi_read_signal(file_id, read_id, signal_length);
  } else {
    signal = read_single_read_signal(file_id, read_id, signal_length);
  }
  
  H5Fclose(file_id);
  return signal;
}

// Free Fast5 signal data
void free_fast5_signal(float *signal) {
  if (signal) {
    free(signal);
  }
}

// **********************************************************************
// Fast5 File Writing Functions
// **********************************************************************

// Write a single-read Fast5 file
int seq_write_fast5_single(const char* filename, seq_tensor** raw_signals,
                           const char** read_names, int num_reads,
                           float sample_rate_khz) {
  // Validate parameters
  if (!filename || !raw_signals || !read_names || num_reads <= 0) {
    warnx("Invalid parameters to seq_write_fast5_single");
    return -1;
  }

  hid_t file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
  if (file_id < 0) {
    errx(EXIT_FAILURE, "Failed to create Fast5 file: %s", filename);
  }

  double version = 1.0;
  hid_t attr_space = H5Screate(H5S_SCALAR);
  hid_t attr = H5Acreate2(file_id, "file_version", H5T_NATIVE_DOUBLE, attr_space, H5P_DEFAULT, H5P_DEFAULT);
  H5Awrite(attr, H5T_NATIVE_DOUBLE, &version);
  H5Aclose(attr);
  H5Sclose(attr_space);

  // Add file_type attribute for single-read format
  hid_t file_type_str_type = H5Tcopy(H5T_C_S1);
  H5Tset_size(file_type_str_type, H5T_VARIABLE);
  H5Tset_cset(file_type_str_type, H5T_CSET_ASCII);
  H5Tset_strpad(file_type_str_type, H5T_STR_NULLTERM);
  hid_t file_type_space_id = H5Screate(H5S_SCALAR);
  hid_t file_type_attr_id = H5Acreate2(file_id, "file_type", file_type_str_type, file_type_space_id, H5P_DEFAULT, H5P_DEFAULT);
  const char* file_type_value = "single-read";
  H5Awrite(file_type_attr_id, file_type_str_type, &file_type_value);
  H5Aclose(file_type_attr_id);
  H5Sclose(file_type_space_id);
  H5Tclose(file_type_str_type);

  // Create parent groups first
  hid_t raw_group_id = H5Gcreate2(file_id, "/Raw", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if (raw_group_id < 0) {
    errx(EXIT_FAILURE, "Failed to create /Raw group");
  }

  hid_t reads_group_id = H5Gcreate2(file_id, "/Raw/Reads", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if (reads_group_id < 0) {
    errx(EXIT_FAILURE, "Failed to create /Raw/Reads group");
  }

  hid_t ugk_group_id = H5Gcreate2(file_id, "/UniqueGlobalKey", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if (ugk_group_id < 0) {
    errx(EXIT_FAILURE, "Failed to create /UniqueGlobalKey group");
  }

  H5Gclose(reads_group_id);
  H5Gclose(raw_group_id);
  H5Gclose(ugk_group_id);

  for (int read_idx = 0; read_idx < num_reads; read_idx++) {
    if (NULL == raw_signals[read_idx]) continue;

    char read_group_path[256];
    snprintf(read_group_path, sizeof(read_group_path), "/Raw/Reads/Read_%d", read_idx);

    hid_t read_group_id = H5Gcreate2(file_id, read_group_path, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

    if (read_group_id < 0) {
      errx(EXIT_FAILURE, "Failed to create read group: %s", read_group_path);
    }

    // Get signal data and length from seq_tensor
    float *signal_data = seq_tensor_data_float(raw_signals[read_idx]);
    size_t signal_length = seq_tensor_dim(raw_signals[read_idx], 0);

    hsize_t signal_dims[1] = {signal_length};
    hid_t signal_space_id = H5Screate_simple(1, signal_dims, NULL);
    hid_t signal_dataset_id = H5Dcreate2(read_group_id, "Signal", H5T_NATIVE_FLOAT, signal_space_id,
                                        H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

    if (H5Dwrite(signal_dataset_id, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                 signal_data) < 0) {
      errx(EXIT_FAILURE, "Failed to write signal data for read %d", read_idx);
    }

    uint32_t duration = (uint32_t)signal_length;
    hid_t duration_space_id = H5Screate(H5S_SCALAR);
    hid_t duration_attr_id = H5Acreate2(read_group_id, "duration", H5T_NATIVE_UINT32, duration_space_id,
                                       H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(duration_attr_id, H5T_NATIVE_UINT32, &duration);

    hid_t str_type = H5Tcopy(H5T_C_S1);
    H5Tset_size(str_type, strlen(read_names[read_idx]) + 1);
    hid_t read_id_space_id = H5Screate(H5S_SCALAR);
    hid_t read_id_attr_id = H5Acreate2(read_group_id, "read_id", str_type, read_id_space_id,
                                      H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(read_id_attr_id, str_type, read_names[read_idx]);

    uint32_t read_number = read_idx;
    hid_t read_number_space_id = H5Screate(H5S_SCALAR);
    hid_t read_number_attr_id = H5Acreate2(read_group_id, "read_number", H5T_NATIVE_UINT32, read_number_space_id,
                                          H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(read_number_attr_id, H5T_NATIVE_UINT32, &read_number);

    int32_t start_mux = 2;
    hid_t start_mux_space_id = H5Screate(H5S_SCALAR);
    hid_t start_mux_attr_id = H5Acreate2(read_group_id, "start_mux", H5T_NATIVE_INT32, start_mux_space_id,
                                        H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(start_mux_attr_id, H5T_NATIVE_INT32, &start_mux);

    uint64_t start_time = 0;
    hid_t start_time_space_id = H5Screate(H5S_SCALAR);
    hid_t start_time_attr_id = H5Acreate2(read_group_id, "start_time", H5T_NATIVE_UINT64, start_time_space_id,
                                         H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(start_time_attr_id, H5T_NATIVE_UINT64, &start_time);

    H5Aclose(start_time_attr_id);
    H5Sclose(start_time_space_id);
    H5Aclose(start_mux_attr_id);
    H5Sclose(start_mux_space_id);
    H5Aclose(read_number_attr_id);
    H5Sclose(read_number_space_id);
    H5Aclose(read_id_attr_id);
    H5Sclose(read_id_space_id);
    H5Tclose(str_type);
    H5Aclose(duration_attr_id);
    H5Sclose(duration_space_id);
    H5Dclose(signal_dataset_id);
    H5Sclose(signal_space_id);
    H5Gclose(read_group_id);
  }

  hid_t channel_group_id = H5Gcreate2(file_id, "/UniqueGlobalKey/channel_id", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

  hid_t str_type = H5Tcopy(H5T_C_S1);
  H5Tset_size(str_type, 2);
  hid_t channel_number_space_id = H5Screate(H5S_SCALAR);
  hid_t channel_number_attr_id = H5Acreate2(channel_group_id, "channel_number", str_type, channel_number_space_id,
                                           H5P_DEFAULT, H5P_DEFAULT);
  H5Awrite(channel_number_attr_id, str_type, "1");

  double digitisation = 8192.0;
  hid_t digitisation_space_id = H5Screate(H5S_SCALAR);
  hid_t digitisation_attr_id = H5Acreate2(channel_group_id, "digitisation", H5T_NATIVE_DOUBLE, digitisation_space_id,
                                         H5P_DEFAULT, H5P_DEFAULT);
  H5Awrite(digitisation_attr_id, H5T_NATIVE_DOUBLE, &digitisation);

  double offset = 0.0;
  hid_t offset_space_id = H5Screate(H5S_SCALAR);
  hid_t offset_attr_id = H5Acreate2(channel_group_id, "offset", H5T_NATIVE_DOUBLE, offset_space_id,
                                   H5P_DEFAULT, H5P_DEFAULT);
  H5Awrite(offset_attr_id, H5T_NATIVE_DOUBLE, &offset);

  double range = 1517.25;
  hid_t range_space_id = H5Screate(H5S_SCALAR);
  hid_t range_attr_id = H5Acreate2(channel_group_id, "range", H5T_NATIVE_DOUBLE, range_space_id,
                                  H5P_DEFAULT, H5P_DEFAULT);
  H5Awrite(range_attr_id, H5T_NATIVE_DOUBLE, &range);

  double sampling_rate = sample_rate_khz * 1000.0;
  hid_t sampling_rate_space_id = H5Screate(H5S_SCALAR);
  hid_t sampling_rate_attr_id = H5Acreate2(channel_group_id, "sampling_rate", H5T_NATIVE_DOUBLE, sampling_rate_space_id,
                                          H5P_DEFAULT, H5P_DEFAULT);
  H5Awrite(sampling_rate_attr_id, H5T_NATIVE_DOUBLE, &sampling_rate);

  H5Aclose(sampling_rate_attr_id);
  H5Sclose(sampling_rate_space_id);
  H5Aclose(range_attr_id);
  H5Sclose(range_space_id);
  H5Aclose(offset_attr_id);
  H5Sclose(offset_space_id);
  H5Aclose(digitisation_attr_id);
  H5Sclose(digitisation_space_id);
  H5Aclose(channel_number_attr_id);
  H5Sclose(channel_number_space_id);
  H5Tclose(str_type);
  H5Gclose(channel_group_id);

  hid_t context_group_id = H5Gcreate2(file_id, "/UniqueGlobalKey/context_tags", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

  str_type = H5Tcopy(H5T_C_S1);
  const char* basename = strrchr(filename, '/') ? strrchr(filename, '/') + 1 : filename;
  H5Tset_size(str_type, strlen(basename) + 1);
  hid_t filename_space_id = H5Screate(H5S_SCALAR);
  hid_t filename_attr_id = H5Acreate2(context_group_id, "filename", str_type, filename_space_id,
                                     H5P_DEFAULT, H5P_DEFAULT);
  H5Awrite(filename_attr_id, str_type, basename);

  H5Aclose(filename_attr_id);
  H5Sclose(filename_space_id);
  H5Tclose(str_type);
  H5Gclose(context_group_id);

  hid_t tracking_group_id = H5Gcreate2(file_id, "/UniqueGlobalKey/tracking_id", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

  str_type = H5Tcopy(H5T_C_S1);
  H5Tset_size(str_type, 20);
  hid_t exp_start_time_space_id = H5Screate(H5S_SCALAR);
  hid_t exp_start_time_attr_id = H5Acreate2(tracking_group_id, "exp_start_time", str_type, exp_start_time_space_id,
                                           H5P_DEFAULT, H5P_DEFAULT);
  H5Awrite(exp_start_time_attr_id, str_type, "2025-01-01T00:00:00");

  // Add required run_id attribute
  hid_t run_id_str_type = H5Tcopy(H5T_C_S1);
  H5Tset_size(run_id_str_type, 40);
  hid_t run_id_space_id = H5Screate(H5S_SCALAR);
  hid_t run_id_attr_id = H5Acreate2(tracking_group_id, "run_id", run_id_str_type, run_id_space_id,
                                   H5P_DEFAULT, H5P_DEFAULT);
  H5Awrite(run_id_attr_id, run_id_str_type, "sequelizer_synthetic_run_001");

  // Add flow_cell_id (length = variable, regular string, not byte string)
  hid_t flowcell_id_str_type = H5Tcopy(H5T_C_S1);
  H5Tset_size(flowcell_id_str_type, H5T_VARIABLE);  // Variable length
  H5Tset_cset(flowcell_id_str_type, H5T_CSET_ASCII); // ASCII, not UTF8
  H5Tset_strpad(flowcell_id_str_type, H5T_STR_NULLTERM);
  hid_t flowcell_id_space_id = H5Screate(H5S_SCALAR);
  hid_t flowcell_id_attr_id = H5Acreate2(tracking_group_id, "flow_cell_id", flowcell_id_str_type, flowcell_id_space_id, H5P_DEFAULT, H5P_DEFAULT);
  // For variable-length strings, you need to write a pointer to the string
  const char* flowcell_value = "FAKE_FC_001";
  H5Awrite(flowcell_id_attr_id, flowcell_id_str_type, &flowcell_value); // Note the &

  // Add device_id (length = variable, regular string, not byte string)
  hid_t device_id_str_type = H5Tcopy(H5T_C_S1);
  H5Tset_size(device_id_str_type, H5T_VARIABLE);  // Variable length
  H5Tset_cset(device_id_str_type, H5T_CSET_ASCII); // ASCII, not UTF8
  H5Tset_strpad(device_id_str_type, H5T_STR_NULLTERM);
  hid_t device_id_space_id = H5Screate(H5S_SCALAR);
  hid_t device_id_attr_id = H5Acreate2(tracking_group_id, "device_id", device_id_str_type, device_id_space_id, H5P_DEFAULT, H5P_DEFAULT);
  // For variable-length strings, you need to write a pointer to the string
  const char* device_value = "SM001";
  H5Awrite(device_id_attr_id, device_id_str_type, &device_value); // Note the &

  H5Aclose(device_id_attr_id);
  H5Sclose(device_id_space_id);
  H5Tclose(device_id_str_type);
  H5Aclose(flowcell_id_attr_id);
  H5Sclose(flowcell_id_space_id);
  H5Tclose(flowcell_id_str_type);
  H5Aclose(run_id_attr_id);
  H5Sclose(run_id_space_id);
  H5Tclose(run_id_str_type);
  H5Aclose(exp_start_time_attr_id);
  H5Sclose(exp_start_time_space_id);
  H5Tclose(str_type);
  H5Gclose(tracking_group_id);

  H5Fclose(file_id);
  return 0;
}

// Write a multi-read Fast5 file
int seq_write_fast5_multi(const char* filename, seq_tensor** raw_signals,
                          const char** read_names, int num_reads,
                          float sample_rate_khz) {
  // Validate parameters
  if (!filename || !raw_signals || !read_names || num_reads <= 0) {
    warnx("Invalid parameters to seq_write_fast5_multi");
    return -1;
  }

  hid_t file_id = H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
  if (file_id < 0) {
    errx(EXIT_FAILURE, "Failed to create Fast5 file: %s", filename);
  }

  // Add file_version attribute
  double version = 1.0;
  hid_t attr_space = H5Screate(H5S_SCALAR);
  hid_t attr = H5Acreate2(file_id, "file_version", H5T_NATIVE_DOUBLE, attr_space, H5P_DEFAULT, H5P_DEFAULT);
  H5Awrite(attr, H5T_NATIVE_DOUBLE, &version);
  H5Aclose(attr);
  H5Sclose(attr_space);

  // Add file_type attribute for multi-read format
  hid_t file_type_str_type = H5Tcopy(H5T_C_S1);
  H5Tset_size(file_type_str_type, H5T_VARIABLE);
  H5Tset_cset(file_type_str_type, H5T_CSET_ASCII);
  H5Tset_strpad(file_type_str_type, H5T_STR_NULLTERM);
  hid_t file_type_space_id = H5Screate(H5S_SCALAR);
  hid_t file_type_attr_id = H5Acreate2(file_id, "file_type", file_type_str_type, file_type_space_id, H5P_DEFAULT, H5P_DEFAULT);
  const char* file_type_value = "multi-read";
  H5Awrite(file_type_attr_id, file_type_str_type, &file_type_value);
  H5Aclose(file_type_attr_id);
  H5Sclose(file_type_space_id);
  H5Tclose(file_type_str_type);

  // Process each read - create root-level read_<read_name> groups
  for (int read_idx = 0; read_idx < num_reads; read_idx++) {
    if (NULL == raw_signals[read_idx]) continue;

    // Create read group path as read_<read_name>
    char read_group_path[256];
    snprintf(read_group_path, sizeof(read_group_path), "read_%s", read_names[read_idx]);

    hid_t read_group_id = H5Gcreate2(file_id, read_group_path, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (read_group_id < 0) {
      errx(EXIT_FAILURE, "Failed to create read group: %s", read_group_path);
    }

    // Create Raw subgroup under the read group
    hid_t read_raw_group_id = H5Gcreate2(read_group_id, "Raw", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (read_raw_group_id < 0) {
      errx(EXIT_FAILURE, "Failed to create Raw subgroup for read: %s", read_names[read_idx]);
    }

    // Get signal data and length from seq_tensor
    float *signal_data = seq_tensor_data_float(raw_signals[read_idx]);
    size_t signal_length = seq_tensor_dim(raw_signals[read_idx], 0);

    // Create Signal dataset in the Raw subgroup
    hsize_t signal_dims[1] = {signal_length};
    hid_t signal_space_id = H5Screate_simple(1, signal_dims, NULL);
    hid_t signal_dataset_id = H5Dcreate2(read_raw_group_id, "Signal", H5T_NATIVE_FLOAT, signal_space_id,
                                        H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

    if (H5Dwrite(signal_dataset_id, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                 signal_data) < 0) {
      errx(EXIT_FAILURE, "Failed to write signal data for read %s", read_names[read_idx]);
    }

    // Add read attributes to the Raw subgroup (not the read group)
    uint32_t duration = (uint32_t)signal_length;
    hid_t duration_space_id = H5Screate(H5S_SCALAR);
    hid_t duration_attr_id = H5Acreate2(read_raw_group_id, "duration", H5T_NATIVE_UINT32, duration_space_id,
                                       H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(duration_attr_id, H5T_NATIVE_UINT32, &duration);

    hid_t str_type = H5Tcopy(H5T_C_S1);
    H5Tset_size(str_type, strlen(read_names[read_idx]) + 1);
    hid_t read_id_space_id = H5Screate(H5S_SCALAR);
    hid_t read_id_attr_id = H5Acreate2(read_raw_group_id, "read_id", str_type, read_id_space_id,
                                      H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(read_id_attr_id, str_type, read_names[read_idx]);

    uint32_t read_number = read_idx;
    hid_t read_number_space_id = H5Screate(H5S_SCALAR);
    hid_t read_number_attr_id = H5Acreate2(read_raw_group_id, "read_number", H5T_NATIVE_UINT32, read_number_space_id,
                                          H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(read_number_attr_id, H5T_NATIVE_UINT32, &read_number);

    int32_t start_mux = 2;
    hid_t start_mux_space_id = H5Screate(H5S_SCALAR);
    hid_t start_mux_attr_id = H5Acreate2(read_raw_group_id, "start_mux", H5T_NATIVE_INT32, start_mux_space_id,
                                        H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(start_mux_attr_id, H5T_NATIVE_INT32, &start_mux);

    uint64_t start_time = 0;
    hid_t start_time_space_id = H5Screate(H5S_SCALAR);
    hid_t start_time_attr_id = H5Acreate2(read_raw_group_id, "start_time", H5T_NATIVE_UINT64, start_time_space_id,
                                         H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(start_time_attr_id, H5T_NATIVE_UINT64, &start_time);

    // Add run_id attribute directly to the read group
    hid_t read_run_id_str_type = H5Tcopy(H5T_C_S1);
    H5Tset_size(read_run_id_str_type, 40);
    hid_t read_run_id_space_id = H5Screate(H5S_SCALAR);
    hid_t read_run_id_attr_id = H5Acreate2(read_group_id, "run_id", read_run_id_str_type, read_run_id_space_id,
                                          H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(read_run_id_attr_id, read_run_id_str_type, "sequelizer_synthetic_run_001");

    // Create channel_id group under this read
    hid_t channel_group_id = H5Gcreate2(read_group_id, "channel_id", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

    hid_t channel_str_type = H5Tcopy(H5T_C_S1);
    H5Tset_size(channel_str_type, 2);
    hid_t channel_number_space_id = H5Screate(H5S_SCALAR);
    hid_t channel_number_attr_id = H5Acreate2(channel_group_id, "channel_number", channel_str_type, channel_number_space_id,
                                             H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(channel_number_attr_id, channel_str_type, "1");

    double digitisation = 8192.0;
    hid_t digitisation_space_id = H5Screate(H5S_SCALAR);
    hid_t digitisation_attr_id = H5Acreate2(channel_group_id, "digitisation", H5T_NATIVE_DOUBLE, digitisation_space_id,
                                           H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(digitisation_attr_id, H5T_NATIVE_DOUBLE, &digitisation);

    double offset = 0.0;
    hid_t offset_space_id = H5Screate(H5S_SCALAR);
    hid_t offset_attr_id = H5Acreate2(channel_group_id, "offset", H5T_NATIVE_DOUBLE, offset_space_id,
                                     H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(offset_attr_id, H5T_NATIVE_DOUBLE, &offset);

    double range = 1517.25;
    hid_t range_space_id = H5Screate(H5S_SCALAR);
    hid_t range_attr_id = H5Acreate2(channel_group_id, "range", H5T_NATIVE_DOUBLE, range_space_id,
                                    H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(range_attr_id, H5T_NATIVE_DOUBLE, &range);

    double sampling_rate = sample_rate_khz * 1000.0;
    hid_t sampling_rate_space_id = H5Screate(H5S_SCALAR);
    hid_t sampling_rate_attr_id = H5Acreate2(channel_group_id, "sampling_rate", H5T_NATIVE_DOUBLE, sampling_rate_space_id,
                                            H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(sampling_rate_attr_id, H5T_NATIVE_DOUBLE, &sampling_rate);

    // Create context_tags group under this read
    hid_t context_group_id = H5Gcreate2(read_group_id, "context_tags", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

    hid_t context_str_type = H5Tcopy(H5T_C_S1);
    const char* basename = strrchr(filename, '/') ? strrchr(filename, '/') + 1 : filename;
    H5Tset_size(context_str_type, strlen(basename) + 1);
    hid_t filename_space_id = H5Screate(H5S_SCALAR);
    hid_t filename_attr_id = H5Acreate2(context_group_id, "filename", context_str_type, filename_space_id,
                                       H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(filename_attr_id, context_str_type, basename);

    // Create tracking_id group under this read
    hid_t tracking_group_id = H5Gcreate2(read_group_id, "tracking_id", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);

    hid_t tracking_str_type = H5Tcopy(H5T_C_S1);
    H5Tset_size(tracking_str_type, 20);
    hid_t exp_start_time_space_id = H5Screate(H5S_SCALAR);
    hid_t exp_start_time_attr_id = H5Acreate2(tracking_group_id, "exp_start_time", tracking_str_type, exp_start_time_space_id,
                                             H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(exp_start_time_attr_id, tracking_str_type, "2025-01-01T00:00:00");

    // Add required run_id attribute
    hid_t run_id_str_type = H5Tcopy(H5T_C_S1);
    H5Tset_size(run_id_str_type, 40);
    hid_t run_id_space_id = H5Screate(H5S_SCALAR);
    hid_t run_id_attr_id = H5Acreate2(tracking_group_id, "run_id", run_id_str_type, run_id_space_id,
                                     H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(run_id_attr_id, run_id_str_type, "sequelizer_synthetic_run_001");

    // Add flow_cell_id (variable length string)
    hid_t flowcell_id_str_type = H5Tcopy(H5T_C_S1);
    H5Tset_size(flowcell_id_str_type, H5T_VARIABLE);
    H5Tset_cset(flowcell_id_str_type, H5T_CSET_ASCII);
    H5Tset_strpad(flowcell_id_str_type, H5T_STR_NULLTERM);
    hid_t flowcell_id_space_id = H5Screate(H5S_SCALAR);
    hid_t flowcell_id_attr_id = H5Acreate2(tracking_group_id, "flow_cell_id", flowcell_id_str_type, flowcell_id_space_id, H5P_DEFAULT, H5P_DEFAULT);
    const char* flowcell_value = "FAKE_FC_001";
    H5Awrite(flowcell_id_attr_id, flowcell_id_str_type, &flowcell_value);

    // Add device_id (variable length string)
    hid_t device_id_str_type = H5Tcopy(H5T_C_S1);
    H5Tset_size(device_id_str_type, H5T_VARIABLE);
    H5Tset_cset(device_id_str_type, H5T_CSET_ASCII);
    H5Tset_strpad(device_id_str_type, H5T_STR_NULLTERM);
    hid_t device_id_space_id = H5Screate(H5S_SCALAR);
    hid_t device_id_attr_id = H5Acreate2(tracking_group_id, "device_id", device_id_str_type, device_id_space_id, H5P_DEFAULT, H5P_DEFAULT);
    const char* device_value = "SM001";
    H5Awrite(device_id_attr_id, device_id_str_type, &device_value);

    // Clean up all resources for this read
    H5Aclose(device_id_attr_id);
    H5Sclose(device_id_space_id);
    H5Tclose(device_id_str_type);
    H5Aclose(flowcell_id_attr_id);
    H5Sclose(flowcell_id_space_id);
    H5Tclose(flowcell_id_str_type);
    H5Aclose(run_id_attr_id);
    H5Sclose(run_id_space_id);
    H5Tclose(run_id_str_type);
    H5Aclose(exp_start_time_attr_id);
    H5Sclose(exp_start_time_space_id);
    H5Tclose(tracking_str_type);
    H5Gclose(tracking_group_id);

    H5Aclose(filename_attr_id);
    H5Sclose(filename_space_id);
    H5Tclose(context_str_type);
    H5Gclose(context_group_id);

    H5Aclose(sampling_rate_attr_id);
    H5Sclose(sampling_rate_space_id);
    H5Aclose(range_attr_id);
    H5Sclose(range_space_id);
    H5Aclose(offset_attr_id);
    H5Sclose(offset_space_id);
    H5Aclose(digitisation_attr_id);
    H5Sclose(digitisation_space_id);
    H5Aclose(channel_number_attr_id);
    H5Sclose(channel_number_space_id);
    H5Tclose(channel_str_type);
    H5Gclose(channel_group_id);

    H5Aclose(start_time_attr_id);
    H5Sclose(start_time_space_id);
    H5Aclose(start_mux_attr_id);
    H5Sclose(start_mux_space_id);
    H5Aclose(read_number_attr_id);
    H5Sclose(read_number_space_id);
    H5Aclose(read_id_attr_id);
    H5Sclose(read_id_space_id);
    H5Tclose(str_type);
    H5Aclose(duration_attr_id);
    H5Sclose(duration_space_id);
    H5Aclose(read_run_id_attr_id);
    H5Sclose(read_run_id_space_id);
    H5Tclose(read_run_id_str_type);
    H5Dclose(signal_dataset_id);
    H5Sclose(signal_space_id);
    H5Gclose(read_raw_group_id);
    H5Gclose(read_group_id);
  }

  H5Fclose(file_id);
  return 0;
}
