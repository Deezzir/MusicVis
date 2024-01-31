# Music Vis

This project aims to create a simple music visualizer using the `C` programming language and the `Raylib` library for rendering. Also, it uses FFT to generate the visualizations.

## Demo

![Screenshot from 2024-01-31 18-41-40](https://github.com/Deezzir/MusicVis/assets/55366304/db2353d1-4137-444d-8b14-a04f45eda571)

Music by `Nu11` and `pilotredsun`.

## Quick Start

To run this project, you need to have `gcc` installed on your machine. Then, you can run the following commands:

```console
make HOTRELOAD=0
./build/musicvis
```

## Hot Reloading

```console
export LD_LIBRARY_PATH="./build/:$LD_LIBRARY_PATH" && make HOTRELOAD=1
./build/musicvis
```

Keep the app running. Rebuild with `./build.sh`. Hot reload by focusing on the window and pressing `F5`.

## Controls

- `Space`: Pause/Play
- `Left Arrow`: Previous song
- `Right Arrow`: Next song
- `Up Arrow`: Increase volume
- `Down Arrow`: Decrease volume
- `F5`: Reload Plugin (only works if `HOTRELOAD` is set to `1`)
- `F | F11`: Toggle fullscreen
- `Delete`: Remove a track that is been hovered
- You can change the order of tracks by hovering on a track and dragging it up or down

