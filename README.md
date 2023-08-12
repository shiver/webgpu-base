Clone the repo and related submodules:
```shell
$ git clone --recurse-submodules https://github.com/shiver/webgpu-base.git
```

Build and run on Windows:
```shell
$ cmake -B build && cmake --build build -j4 && build\Debug\app.exe
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

Then in the browser go to `http://localhost:8080/build-web/app.html`.
