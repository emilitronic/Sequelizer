// **********************************************************************
// sequelizer_subcommands.h
// **********************************************************************
// S Magierowski Jul 26 2025

#ifndef SEQUELIZER_SUBCOMMANDS_H
#define SEQUELIZER_SUBCOMMANDS_H

#include <stdbool.h>
#include <stdio.h>
#include "sequelizer_fast5.h"
#include "sequelizer_convert.h"
#include "sequelizer_plot.h"
#include "sequelizer_seqgen.h"
#include "sequelizer_bcall.h"

enum sequelizer_mode {SEQUELIZER_MODE_HELP=0,
                      SEQUELIZER_MODE_SEQGEN,
                      SEQUELIZER_MODE_FAST5,
                      SEQUELIZER_MODE_CONVERT,
                      SEQUELIZER_MODE_PLOT,
                      SEQUELIZER_MODE_BCALL,
                      SEQUELIZER_MODE_INVALID };
static const enum sequelizer_mode sequelizer_ncommand = SEQUELIZER_MODE_INVALID;

enum sequelizer_mode get_sequelizer_mode(const char *modestr);
const char *sequelizer_mode_string(const enum sequelizer_mode mode);
const char *sequelizer_mode_description(const enum sequelizer_mode mode);
int fprint_sequelizer_commands(FILE * fp, bool header);

int main_help_short(void);
int main_seqgen(int argc, char *argv[]);
int main_fast5(int argc, char *argv[]);
int main_convert(int argc, char *argv[]);
int main_plot(int argc, char *argv[]);

#endif //SEQUELIZER_SUBCOMMANDS_H