.PHONY: all clean install

all:
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build

clean:
    rm -rf build install

install:
    cmake --install build
