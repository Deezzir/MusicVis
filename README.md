# Music Vis

This is a project that aims to create a simple music visualizer using the `C` programming language and the `Raylib` library for rendering. Also, it uses FFT to generate the visualizations.

## Demo

## Quick Start

https://github.com/Deezzir/MusicVis/assets/55366304/7b712b2d-e2f8-487f-a45c-fd1a0bac5593

To run this project, you need to have `gcc` installed on your machine. Then, you can run the following commands:

```console
./build.sh
./musicvis
```

## Hot Reloading

```console
export HOTRELOAD=1
export LD_LIBRARY_PATH="./build/:$LD_LIBRARY_PATH"
./build.sh
./build/musicvis
```

Keep the app running. Rebuild with `./build.sh`. Hot reload by focusing on the window of the app and pressing `R`.
