// **********************************************************************
// sequelizer_subcommands.c
// **********************************************************************
// S Magierowski Jul 26 2025
//
#include "../include/sequelizer.h"
#include "sequelizer_subcommands.h"

enum sequelizer_mode get_sequelizer_mode(const char *modestr) {
  if (0 == strcmp(modestr, "help")) {
    return SEQUELIZER_MODE_HELP;
  }
  if (0 == strcmp(modestr, "seqgen")) {
    return SEQUELIZER_MODE_SEQGEN;
  }
  if (0 == strcmp(modestr, "fast5")) {
    return SEQUELIZER_MODE_FAST5;
  }
  if (0 == strcmp(modestr, "convert")) {
    return SEQUELIZER_MODE_CONVERT;
  }
  if (0 == strcmp(modestr, "plot")) {
    return SEQUELIZER_MODE_PLOT;
  }
  if (0 == strcmp(modestr, "bcall")) {
    return SEQUELIZER_MODE_BCALL;
  }
  return SEQUELIZER_MODE_INVALID;
}

const char *sequelizer_mode_string(const enum sequelizer_mode mode) {
  switch (mode) {
    case SEQUELIZER_MODE_HELP:
      return "help";
    case SEQUELIZER_MODE_SEQGEN:
      return "seqgen";
    case SEQUELIZER_MODE_FAST5:
      return "fast5";
    case SEQUELIZER_MODE_CONVERT:
      return "convert";
    case SEQUELIZER_MODE_PLOT:
      return "plot";
    case SEQUELIZER_MODE_BCALL:
      return "bcall";
    case SEQUELIZER_MODE_INVALID:
      errx(EXIT_FAILURE, "Invalid Sequelizer mode\n");
    default:
      errx(EXIT_FAILURE, "Sequelizer enum failure -- report bug\n");
  }
  return NULL;
}

const char *sequelizer_mode_description(const enum sequelizer_mode mode) {
  switch (mode) {
    case SEQUELIZER_MODE_HELP:
      return "Show help message";
    case SEQUELIZER_MODE_SEQGEN:
      return "Generate synthetic sequences and signals";
    case SEQUELIZER_MODE_FAST5:
      return "Fast5 file operations";
    case SEQUELIZER_MODE_CONVERT:
      return "File format conversion";
    case SEQUELIZER_MODE_PLOT:
      return "Signal visualization and plotting";
    case SEQUELIZER_MODE_BCALL:
      return "Basecall raw nanopore signals to DNA sequences";
    case SEQUELIZER_MODE_INVALID:
      errx(EXIT_FAILURE, "Invalid Sequelizer mode\n");
    default:
      errx(EXIT_FAILURE, "Sequelizer enum failure -- report bug\n");
  }
  return NULL;
}

int fprint_sequelizer_commands(FILE * fp, bool header) {
  if (header) {
    fprintf(fp, "COMMANDS:\n");
  }
  for (enum sequelizer_mode mode = SEQUELIZER_MODE_HELP; mode < sequelizer_ncommand; mode++) {
    fprintf(fp, "  %-8s %s\n", sequelizer_mode_string(mode), sequelizer_mode_description(mode));
  }
  return EXIT_SUCCESS;
}

int main_help_short(void) {
  printf("Sequelizer - DNA Sequence Analysis Toolkit\n\n");
  printf("Basic usage:\n");
  printf("* sequelizer help          Print detailed help\n");
  printf("* sequelizer seqgen        Generate synthetic sequences and signals\n");
  printf("* sequelizer fast5         Fast5 file operations\n");
  printf("* sequelizer convert       File format conversion\n");
  printf("* sequelizer plot          Signal visualization and plotting\n");
  printf("* sequelizer bcall         Basecall raw nanopore signals to DNA sequences\n");
  printf("\nFor more information: sequelizer help\n");
  return EXIT_SUCCESS;
}

// Note: main_seqgen() is now implemented in sequelizer_seqgen.c

