---
layout: default
title: seqgen Command
---
#  seqgen Command

Sequelizer's `seqgen` subcommand creates physical signals from textual DNA sequences. 

 It can read sequences from FASTA/FASTQ files or generate random sequences (i.e., ACGT strings), and simulate the nanopore signals that would be produced when those sequences are processed by a nanopore sensor.

## Selecting reads
Used the `--select` option to pick out the reads that you want to generate on (you only need quotes if you use special characters like spaces and *).
  1. Name pattern: `--select "6ea6609b"` or `--select 6ea6609b`
  2. Multiple names: `--select "6ea6609b,7b02d4c4"` or `--select 6ea6609b,7b02d4c4`
  3. Single index: `--select "1"` or `--select 1`
  4. Full range: `--select "101:110"` or `--select 101:110`
  5. Open-ended: `--select "10:"` or `--select 10:`
  6. Start range: `--select ":12"`or `--select :12`
  7. Multiple selections: `--select "1,3,5"` or `--select 1,3,5`
  8. Mixed: `--select "1:2,8:12"` or `--select 1:2,8:12`