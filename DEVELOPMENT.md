## Build on windows

Creating a DuckDB extension on Windows using MinGW on MSYS2 requires several steps, including setting up your environment, obtaining the necessary source code and dependencies, and compiling the extension. Here’s a detailed guide:

Sources

- https://duckdb.org/dev/building.html
- https://github.com/duckdb/duckdb/blob/v0.8.1/.github/workflows/Windows.yml#L158


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


## 📦 Deploying the extension (TODO)
To deploy the extension, simply run:
```sh ./scripts/extension_upload.sh```


## Debuging SAP behavior

### Relevant transctions

- [SM04](https://help.sap.com/doc/saphelp_nw74/7.4.16/en-us/02/3b3ad97b0b4526839377f4c2112f33/content.htm?no_cache=true) Login List
- [SM50](https://help.sap.com/doc/saphelp_nw74/7.4.16/en-us/19/28d51a81c748b399947f3e354d2ffb/content.htm?no_cache=true) Work Process Overview
- [ST02](https://help.sap.com/docs/ABAP_PLATFORM_BW4HANA/f146e75588924fa4987b6c8f1a7a8c7e/ce7a5224577d4713b1d695bdf9baf656.html) Memory Statistics / Tune Summary
- [ST03](https://help.sap.com/saphelp_gbt10/helpdata/EN/2d/b8be3befaefc75e10000000a114084/frameset.htm) Workload and Performance Statistics
- [SM21](https://help.sap.com/doc/saphelp_nw75/7.5.5/de-DE/b1/f4652c0f4d4e8fa04e165d161e386f/content.htm?no_cache=true) System Log
- [DBACOCKPIT]() DBA Cockpit