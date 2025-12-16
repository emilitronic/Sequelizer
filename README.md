<div align="center">

<picture>
  <img alt="sequelizer" src="docs/sequelizer.png" width="20%" height="20%">
</picture>

**Nanopore bioinformatics for edge computing**

</div>

---

## About

Analyze DNA sequences in real-time, on-device, across the edge network. 

Sequelizer brings efficient bioinformatics to embedded sequencers, smartphones, and edge servers -
processing nanopore data streams as they're generated, not hours later in the cloud.

Main [Sequelizer](https://emilitronic.github.io/Sequelizer/) documentation page (under construction)

## Installation

**Required:**
```bash
# macOS
brew install argp-standalone hdf5
# Ubuntu/Debian (may need to drop libargp-dev if argp functionality is already built-in)
sudo apt-get update && sudo apt-get install -y libhdf5-dev libopenblas-dev pkg-config libcunit1-dev libargp-dev 
# Build tools
# CMake 3.23+ and C++17 compiler required
```

**Optional (for testing):**
```bash
# For plotting capabilities
brew install gnuplot feedgnuplot    # macOS
sudo apt-get install gnuplot        # Ubuntu
# For unit tests
brew install cunit                  # macOS
sudo apt-get install libcunit1-dev  # Ubuntu
```


### Build
```bash
mkdir build && cd build
cmake .. && cmake --build .
cd ..  # Return to sequelizer directory
```

### Quick Start
Run commands from the `sequelizer/` directory (not `build/`):
```bash
# Analyze Fast5 files
./build/sequelizer fast5 data.fast5 --recursive --verbose

# Convert to text format
./build/sequelizer convert data.fast5 --to raw

# Plot signals
./build/sequelizer plot signals.txt

# Generate sequences (requires kmer_models/)
./build/sequelizer seqgen -M
```

---

**[Full Documentation](https://emilitronic.github.io/Sequelizer/)** • **[Getting Started](https://emilitronic.github.io/Sequelizer/getting-started)** • **[Command Reference](https://emilitronic.github.io/Sequelizer/sequelizer_commands)** • **[Fast5 Compatibility](https://emilitronic.github.io/Sequelizer/fast5_compatibility)**
