# Music Vis

This is a project that aims to create a simple music visualizer using the `C` programming language and the `Raylib` library for rendering. Also, it uses FFT to generate the visualizations.

## Demo

![Screenshot from 2024-01-02 03-07-20](https://github.com/Deezzir/MusicVis/assets/55366304/10fe5d7f-c1b3-4b26-a752-ddf53315524c)

Music by `Nu11` and `pilotredsun`.

## Quick Start

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

Keep the app running. Rebuild with `./build.sh`. Hot reload by focusing on the window of the app and pressing `F5`.

## Controls

- `Space`: Pause/Play
- `Left Arrow`: Previous song
- `Right Arrow`: Next song
- `Up Arrow`: Increase volume
- `Down Arrow`: Decrease volume
- `F5`: Reload Plugin (only works if `HOTRELOAD` is set to `1`)
- `F | F11`: Toggle fullscreen
- `Delete`: Remove a track that is been hovered
