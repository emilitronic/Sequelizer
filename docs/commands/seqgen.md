---
layout: default
title: seqgen Command
---
#  seqgen Command

## Selecting reads
Used the `--select` option to pick out the reads that you want to generate on (you only need quotes if you use special characters like spaces and *).
  1. Name pattern: `--select "6ea6609b"` or `--select 6ea6609b`
  2. Single index: `--select "1"` or `--select 1`
  3. Full range: `--select "101:110"` or `--select 101:110`
  4. Open-ended: `--select "10:"` or `--select 10:`
  5. Start range: `--select ":12"`or `--select :12`