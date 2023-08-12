Clone the repo and related submodules:
```shell
$ git clone --recurse-submodules https://github.com/shiver/webgpu-base.git
```

I have had some issues with Dawn correctly detecting the python executable path.
I've found that if I simply hard-code the `PYTHON_EXECUTABLE` environment variable that it can help.
EG:
```shell
# Windows
$ set PYTHON_EXECUTABLE=d:/Python39/python.exe

# MacOS
$ set PYTHON_EXECUTABLE=/usr/local/bin/python3
```

Build and run on Windows:
```shell
$ cmake -B build && cmake --build build -j4 && build\Debug\app.exe
```

Build and run on MacOS:
```shell
$ cmake -B build && cmake --build build -j4 && build/app
```

To build for web you'll first need to download and install Emscripten. Once that is done you can proceed with the following steps:
```shell
$ emsdk activate latest
$ emcmake cmake -DCMAKE_BUILD_TYPE=Debug -B build-web && cmake --build build-web -j4
```

You won't be able to run the `HTML` file directly.
Use Python, or something similar to run a local server:
```
$ python -m http.server 8080
```

Then in the browser go to `http://localhost:8080/build-web/app.html`.
