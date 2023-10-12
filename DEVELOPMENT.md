## Build on windows

Creating a DuckDB extension on Windows using MinGW on MSYS2 requires several steps, including setting up your environment, obtaining the necessary source code and dependencies, and compiling the extension. Here’s a detailed guide:

1. **Install MSYS2 and MinGW:**

   - Download MSYS2 installer from the [official website](https://www.msys2.org/).
   - Run the installer and follow the instructions to install MSYS2 on your machine.
   - Open MSYS2 terminal and update the package database and core system packages with: `pacman -Syu`.
   - Install MinGW-w64 by running: `pacman -S git mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja`.

2. **Setup Environment:**

   - Close and reopen MSYS2 terminal.
   - Add MinGW to your system PATH: `export PATH="/mingw64/bin:$PATH"`.

3. **Download Extension Source Code:**
   - Clone the ERPL repository: `git clone https://github.com/duckdb/duckdb.git`.
   - Fetch the `duckdb` submodule after clone:  `git submodule update --init --recursive`.
   - Navigate to the duckdb directory: `cd duckdb` and checkout the `v0.9.0` tag: `git checkout v0.9.0`.

4. **Download sapnwrfc Library:**
   - Download the sapnwrfc library from the [official website](https://support.sap.com/en/product/connectors/nwrfcsdk.html) (or the Zip from our Google Drive).
   - Extract the downloaded archive and copy the `sapnwrfc` directory to the `sapnwrfc` directory in the erpl repository.

5. **Configure and Build:**
   - Run the following command in the root directory of the extension: `mingw32-make release`.

6. **Testing Your Extension:**
   - Create a directory (e.g. `test`).
   - Copy the following artifacts to that directory.
    - The built duckdb executable (e.g. `.\build\release\duckdb.exe`).
    - The built erpl extension (e.g. `.\build\release\extension\erpl\erpl.duckdb_extension`).
    - All `*.dll` files from `.\sapnwrfc\lib`.
   - Run duckdb with the following command: `.\duckdb.exe -unsigned`.
   - In the DuckDB shell_
        - First install the erpl-extension with: `INSTALL 'erpl.duckdb_extension';`.
        - And then load the extension with: `LOAD 'erpl';`.
