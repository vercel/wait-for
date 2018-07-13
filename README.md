# wait-for

A small utility that waits for a file to exist, optionally with some permissions.

## Building

Either via Docker:

```console
$ git submodule update --init --recursive
$ docker build -t zeit/wait-for .
```

or using CMake (requires Linux)

```console
$ git submodule update --init --recursive
$ mkdir build && cd build
$ cmake .. -DCMAKE_BUILD_TYPE=Release
$ cmake --build .
```

## Usage

```
wait-for [--help] [-rwx] [-dfps] [-U <username>] <file>

Wait for a file to exist and optionally have one or modes

-h, --help          Shows this help message
-x, --execute       Wait for the path to become executable
-r, --read          Wait for the path to become readable
-w, --write         Wait for the path to become writable
-p, --pipe          Wait for the path to be a pipe (FIFO)
-s, --socket        Wait for the path to be a socket
-f, --file          Wait for the path to be a regular file
-d, --directory     Wait for the path to be a directory
-U, --username      The username to run access checks for (NOT the user ID)

If multiple modes are specified, wait-for waits for all of them to become available.
If multiple file types are specified, wait-for waits for the file to be any one of the specified types.
```

# License
Copyright &copy; 2018 ZEIT, Inc. Released under the [MIT License](LICENSE.md).
