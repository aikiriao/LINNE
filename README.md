**このリポジトリはテスト追加となんらかの論文投稿が完了次第publicに移動します**

# LINNE

LInear-predictive Neural Net Encoder

# How to build

## Requirement

* [CMake](https://cmake.org) >= 3.15

## Build LINNE Codec

```bash
git clone https://github.com/ShounoLab/LINNE.git
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
./linne -e -m 2 INPUT.wav OUTPUT.lnn
```

### Decode

```bash
./linne -d INPUT.lnn OUTPUT.wav
```

## License

MIT
