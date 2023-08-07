Build native:
```shell
$ cmake -B build && cmake --build build -j4
```

Build web:
```shell
$ emsdk activate latest
$ emcmake cmake -DCMAKE_BUILD_TYPE=Debug -B build-web && cmake --build build-web -j4
```

You won't be able to run the `HTML` file directly. Use Python (or something else to run a local server instead:
```
$ cd build-web
$ python -m http.server 8080
```
