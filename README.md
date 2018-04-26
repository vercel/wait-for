# wait-for

A small utility that waits for a file to exist, optionally with some permissions.

## Building

Either via Docker:

```console
$ docker build -t zeit/wait-for .
```

or using CMake (requires Linux)

```console
$ mkdir build && cd build
$ cmake .. -DCMAKE_BUILD_TYPE=Release
$ cmake --build .
```

## Usage

```
wait-for [--help] [-rwx] <file>

Waits for a file to exist and optionally have one or modes

-h, --help         Shows this help message
-x, --execute      Wait for the file to become executable
-r, --read         Wait for the file to become readable
-w, --write        Wait for the file to become writable
-U, --username     The username to run access checks for (NOT the user ID)

If multiple modes are specified, wait-for waits for all of them to become available
```

# License
Copyright &copy; 2018 ZEIT, Inc. Released under the [MIT License](LICENSE.md).
