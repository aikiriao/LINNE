# LINNE

LInear-predictive Neural Net Encoder

# How to build

## Requirement

* [CMake](https://cmake.org) >= 3.15

## Build LINNE Codec

```bash
git clone https://github.com/aikiriao/LINNE.git
cd LINNE/tools/linne_codec
cmake -B build
cmake --build build
```

# Usage

## LINNE Codec

### Encode

```bash
./linne -e INPUT.wav OUTPUT.lnn
```

you can change compression mode by `-m` option.
Following example encoding in maximum compression (but slow) option.

```bash
./linne -e -m 3 INPUT.wav OUTPUT.lnn
```

### Decode

```bash
./linne -d INPUT.lnn OUTPUT.wav
```

## License

MIT
