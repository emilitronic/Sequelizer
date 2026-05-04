// **********************************************************************
// Sequelizer: C-based DNA Sequence Analysis Toolkit
// **********************************************************************
// Sebastian Claudiusz Magierowski Jul 26 2025
//
// to build:
// mkdir build
// cd build
// cmake ..
// cmake --build .
// Get general command list: sequelizer help
// Or run your command & subcommand
// e.g.,
// sequelizer seqgen --nanopore --length 1000 -o synthetic.fast5

#include <string.h>   /*  ←  for strcmp  */
#include <err.h>
#include "../include/sequelizer.h"
#include "sequelizer_subcommands.h"

int main(int argc, char *argv[]) {

  /* 1. No arguments → short help  */
  if (argc == 1) {
    return main_help_short();
  }

  /* 2. Handle `--help` or `-h` **before** sub-command switch  */
  if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)
    return main_help_short();

  /* 3. Normal sub-command dispatch */
  int ret = EXIT_FAILURE;
  switch (get_sequelizer_mode(argv[1])) {
    case SEQUELIZER_MODE_SEQGEN:
      ret = main_seqgen(argc - 1, argv + 1);
      break;
    case SEQUELIZER_MODE_FAST5:
      ret = main_fast5(argc - 1, argv + 1);
      break;
    case SEQUELIZER_MODE_CONVERT:
      ret = main_convert(argc - 1, argv + 1);
      break;
    case SEQUELIZER_MODE_PLOT:
      ret = main_plot(argc - 1, argv + 1);
      break;
    case SEQUELIZER_MODE_BCALL:
      ret = main_bcall(argc - 1, argv + 1);
      break;
    case SEQUELIZER_MODE_HELP:
      ret = main_help_short();
      break;
    default:
      ret = EXIT_FAILURE;
      warnx("Unrecognised subcommand '%s'\n", argv[1]);
      printf("Try 'sequelizer help' for available commands.\n");
  }
  return ret;
}
