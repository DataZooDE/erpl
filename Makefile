.PHONY: all clean debug release pull update impl trampoline

all: release

clean:
ifeq ($(OS),Windows_NT)
	powershell.exe -Command "if (Test-Path -Path ./build) { Remove-Item -Recurse -Force ./build }"
	powershell.exe -Command "if (Test-Path -Path ./duckdb/build) { Remove-Item -Recurse -Force ./duckdb/build }"
else
	rm -rf ./build
	cd ./duckdb && make clean
	cd ./duckdb && make clean-python
endif

debug:
	cd impl && make debug
	cd trampoline && make debug

release:
	cd impl && make release
	cd trampoline && make release

update:
	git submodule update --remote --merge

pull:
	git submodule init
	git submodule update --recursive --remote




