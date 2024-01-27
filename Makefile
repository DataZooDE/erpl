.PHONY: 
	all clean 
	debug debug_rfc debug_odp debug_trampoline 
	release release_rfc release_odp release_bics 
	pull update

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

debug_rfc:
	cd rfc && make debug

debug_odp: debug_rfc
ifeq ($(wildcard odp/Makefile), odp/Makefile)
	@echo "Running debug in ODP directory"
	cd odp && make debug
else
	@echo "ODP directory with Makefile not found, skipping debug_odp"
endif

debug_bics: debug_rfc
ifeq ($(wildcard bics/Makefile), bics/Makefile)
	@echo "Running debug in BICS directory"
	cd bics && make debug
else
	@echo "BICS directory with Makefile not found, skipping debug_bics"
endif

debug_trampoline: debug_rfc debug_odp debug_bics
	cd trampoline && make debug

debug: debug_trampoline

release_rfc:
	cd rfc && make release

release_odp: release_rfc
ifeq ($(wildcard odp/Makefile), odp/Makefile)
	@echo "Running release in ODP directory"
	cd odp && make release
else
	@echo "ODP directory with Makefile not found, skipping release_odp"
endif

release_bics: release_rfc
ifeq ($(wildcard bics/Makefile), bics/Makefile)
	@echo "Running release in BICS directory"
	cd bics && make release
else
	@echo "BICS directory with Makefile not found, skipping release_bics"
endif

release_trampoline: release_rfc release_odp release_bics
	cd trampoline && make release

release: release_trampoline

update:
	git submodule update --remote --merge

pull:
	git submodule init
	git submodule update --recursive --remote




