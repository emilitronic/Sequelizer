// **********************************************************************
// sequelizer_bcall.c
// **********************************************************************
// Sebastian Claudiusz Magierowski May 4 2026
/*
Basecaller subcommand: converts raw nanopore signals (Fast5) to DNA sequences.

Example uses (intended — not yet implemented):
  Show help and options:        sequelizer bcall --help
  Basic basecalling:            sequelizer bcall -i signals.fast5 -o calls.fa
  Directory input (recursive):  sequelizer bcall -i data/ -o calls.fa --recursive
  Specify model:                sequelizer bcall -i signals.fast5 -o calls.fa --model socall
  Limit reads:                  sequelizer bcall -i data/ -o calls.fa --limit 100
*/

#include "sequelizer_bcall.h"
#include <stdbool.h>
#include <argp.h>
#include <err.h>

static char doc[]      = "Sequelizer basecaller - convert raw nanopore signals to DNA sequences";
static char args_doc[] = "[options]";

static struct argp_option options[] = {
  {"input",     'i', "path",   0, "Input Fast5 file or directory"},
  {"output",    'o', "file",   0, "Output file for basecalled sequences"},
  {"model",     'm', "name",   0, "Basecalling model to use (default: socall)"},
  {"limit",     'l', "nreads", 0, "Maximum number of reads to process (0 = unlimited)"},
  {"recursive", 'r', 0,        0, "Search subdirectories recursively for Fast5 files"},
  {0}
};

struct bcall_args {
  char *input_path;
  char *output_path;
  char *model_name;
  int   limit;
  bool  recursive;
};

static struct bcall_args args = {
  .input_path  = NULL,
  .output_path = NULL,
  .model_name  = "socall",
  .limit       = 0,
  .recursive   = false,
};

static int parse_arg(int key, char *arg, struct argp_state *state) {
  (void)state;
  switch (key) {
    case 'i': args.input_path  = arg;        break;
    case 'o': args.output_path = arg;        break;
    case 'm': args.model_name  = arg;        break;
    case 'r': args.recursive   = true;       break;
    case 'l':
      args.limit = atoi(arg);
      if (args.limit < 0) errx(EXIT_FAILURE, "Limit must be non-negative: %s", arg);
      break;
    case ARGP_KEY_END:
      if (!args.input_path)
        argp_error(state, "No input specified. Use --input / -i.");
      if (!args.output_path)
        argp_error(state, "No output specified. Use --output / -o.");
      break;
    default:
      return ARGP_ERR_UNKNOWN;
  }
  return 0;
}

static struct argp argp = {options, parse_arg, args_doc, doc};

static void print_configuration(void) {
  printf("Sequelizer basecaller configuration:\n");
  printf("  Input:     %s\n", args.input_path);
  printf("  Output:    %s\n", args.output_path);
  printf("  Model:     %s\n", args.model_name);
  if (args.limit > 0) printf("  Limit:     %d reads\n", args.limit);
  else                printf("  Limit:     unlimited\n");
  printf("  Recursive: %s\n", args.recursive ? "yes" : "no");
}

int main_bcall(int argc, char *argv[]) {
  argp_parse(&argp, argc, argv, 0, 0, 0);
  print_configuration();
  printf("\nbcall: not yet implemented\n");
  return EXIT_SUCCESS;
}
