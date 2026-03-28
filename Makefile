.PHONY: all clean

all: build/build.ninja
	ninja -C build

build/build.ninja:
	cmake -B build -G Ninja

clean:
	rm -rf build
