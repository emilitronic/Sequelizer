---
layout: default
title: Examples
---
# Examples

## Consider making a shortcut

See [Getting Started](../getting-started.md) to learn how to build Sequelizer.  First, the build instructions leave you with a `sequelizer` binary in your `build` directory.  You might want to alias it for easier use:
```bash
some_dir $ alias sequelizer='<whatever_your_path_is>/build/sequelizer'
```
Now you can run Sequelizer commands with `sequelizer`.  The examples below assume you have done this.

## Sequelizer Examines Fast5's
You can use Sequelizer to analyze Fast5 files.  A mature file format that holds nanopore information, both raw DNA measurements and lots of useful metadata.  We (and you?) want circuits, computers, programs, networks working on this data to extract relevant genomic inisights anytime, anywhere.

To play around you'll want some Fast5 files on your system.  Here's how to download a sample dataset 
```bash
# Get some Fast5 files (this is ~2.4GB, with a 15MB/s download, it should take ~2.5 min)
some_dir $ wget https://ont-exd-int-s3-euwst1-epi2me-labs.s3-eu-west-1.amazonaws.com/fast5_tutorial/sample_fast5.tar
# Uncompress
some_dir $ tar -xvf sample_fast5.tar
# See what you got
some_dir $ ls
... sample_fast5/ ...
```
Now that you have some data in the `sample_fast5` directory, you can use Sequelizer to analyze the Fast5 files.  You do this using the package's `fast5` subcommand. Just point it to the directory with the `--recursive` flag to find all Fast5 files within:
```bash
# sequelize it
some_dir $ sequelizer fast5 sample_fast5 --recursive
Discovering Fast5 files...
Found 5 files, analyzing...
[[████████████████████████████████████████] 100% (5/5)

Sequelizer Fast5 Dataset Analysis Summary
=========================================
Files processed: 5/5 successful
Total file size: 2440.0 MB
Total reads: 20000
Signal statistics:
  Total samples: 679508454
  Average length: 33975 samples
  Range: 1978 - 1199785 samples
  Average bits per sample: 28.73
  Total duration: 2831.3 minutes
  Avg duration: 8.5 seconds
Processing time: 43.02 seconds
```
What happened?  Sequelizer scanned the Fast5 files, extracted some basic statistics about the raw signal data, and printed a summary report.  You can see how many reads were processed in the directory you scanned, their lengths, and other useful information.  Almost 30 bits per sample were used to store the raw signal data?  Awful if true.

If you want to know a little more about each read that was processed you could ask for a summary report to be made.
```bash
some_dir $ sequelizer fast5 sample_fast5 --recursive --summary
```
This will create a `sequelizer_summary.txt` file in your current directory with a line for each read processed, and some basic statistics about it.  For example:
```text
#sequelizer_summary_v1.0
filename	read_id	run_id	channel	start_time	translocation_time	num_samples	median_before
end_reason_datatype_uint8_t.fast5	008868ec-1f4b-472b-80f7-62fc23f3c51f	355bdcb8c31448c7e96a4113bcfa15c6921e86c3	  11	  105.7	   5.9	 23469	 231.02
end_reason_differnt_key_order.fast5	007c5024-e963-40ac-abd7-c425594c6404	5c7dcc184bcf81678cbe2cc6387c4bf9908009e7	 341	 1654.1	   1.3	  5298	 210.87
...
```

## Sequelizer Converts Fast5's
Sequelizer can also convert Fast5 files to other formats (well one format, for now).  For example, you can convert Fast5 files to txt using the `convert` subcommand:
```bash
some_dir  $ sequelizer convert sample_fast5 --to raw -r  
[████████████████████████████████████████] 100% (5/5)
```
In this case, the `--to raw` flag tells Sequelizer to convert the Fast5 files to raw text files.  The `-r` flag tells it to search recursively through the `sample_fast5` directory for Fast5 files.  After running this command, you will find `.txt` files in `some_dir`.  For single-read Fast5 files the default file name takes the form `read_ch<channel num>_rd<read num>.txt`.  For multi-read Fast5 files the default file name takes the form `<original fast5 files name>_read_ch<channe _num>_rd<read num>.txt`.  In this example, the Fast5 files in `sample_fast5` are multi-read files, so you should see files like:
```bash
some_dir $ ls
FAK42335_2bf4f211a2e2d04662e50f27448cfd99dafbd7ee_0_read_ch323_rd17.txt
FAK42335_2bf4f211a2e2d04662e50f27448cfd99dafbd7ee_0_read_ch381_rd59.txt
FAK42335_2bf4f211a2e2d04662e50f27448cfd99dafbd7ee_0_read_ch412_rd172.txt
FAK42335_2bf4f211a2e2d04662e50f27448cfd99dafbd7ee_100_read_ch108_rd3140.txt
...
```
By default, `convert` only extracts the first three reads from each Fast5 file that it finds.  You can change this with the `--all` flag which tells Sequelizer to extract all reads from each Fast5 file.

What's inside each `.txt` file?  Behold:
```text
# Channel: 198
# Offset: 6.000000
# Range: 1440.801147
# Digitisation: 8192.000000
# Conversion: signal_pA = (raw_signal + offset) * range / digitisation
# Sample Rate: 4000.0
# Read ID: 00213bd6-0b7d-4e96-862f-160852db369a
#
sample_index	raw_sample
0	441
1	465
2	484
3	455
...
```
The header contains metadata about the read, including channel number, offset, range, digitization, conversion formula, sample rate, and read ID.  Below the header is a two-column table with the sample index and the corresponding raw signal value.

## Before You Go
Please help Sequelizer development.  Tell us what else you'd like to see here!