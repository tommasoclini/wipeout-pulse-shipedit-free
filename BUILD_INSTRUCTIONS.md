## Build instructions

The program has been built on debian, building on windows is not recommended because of dogshit development environment.

Install necessary dependencies for building:

```bash
sudo apt update &&
sudo apt install make cmake git wget zip mingw-w64 xz-utils
```

After that, for downloading required libraries and building there is a [script](mingw/build-win32.sh) available, so, from the repo directory:

```bash
bash mingw/build-win32.sh
```

After it's done, a zip file containing the exe, README, LICENSE and editor.wad, and the separate exe for both 32 and 64 bit windows will be generated in the repo directory.
