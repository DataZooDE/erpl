# ★ Erpl Extension
The main goal of this [DuckDB](https://duckdb.org) extension is to provide easy access to SAP data ecosystem. This should be done with 
*minimal dependencies* and as *easy and natural* as possible from the perspective of the DuckDB User.

We have two man target use cases:
- Accessing data from SAP ERP (via RFC) or SAP BW (via BICS) for interactiver Data Science and Analytics use cases.
- Replicating data from SAP ERP or SAP BW to DuckDB in DuckDB based data pipelines.

## Getting started
First step to getting started is to clone this template onto your local computer. Then clone your new repository using 
```sh
git clone --recurse-submodules https://bitbucket.org/erpel/erpel.git
```
Note that `--recurse-submodules` will ensure the correct version of duckdb is pulled. This is necessary to make the DuckDB based extension build system work.
Another dependency is the SAP Netweaver RFC SDK. This is not included in the template, but can be downloaded from the SAP Service Marketplace. The resulting 
zip file should be placed in the `./nwrfcsdk` folder. The build system will automatically detect the SDK and build the extension against it.

## ⚙ Building from the shell
To build the extension:
```sh
make
```
The main binaries that will be built are:
```sh
./build/release/duckdb
./build/release/test/unittest
./build/release/extension/<extension_name>/<extension_name>.duckdb_extension
```
- `duckdb` is the binary for the duckdb shell with the extension code automatically loaded. 
- `unittest` is the test runner of duckdb. Again, the extension is already linked into the binary.
- `<extension_name>.duckdb_extension` is the loadable binary as it would be distributed.

## ⚙ Builidng from VSCode IDE
(The configuration IMHO would be a good candiate for a blog post).
We spent some time to make the extension buildable from VSCode IDE. The main challenge was to get the build system to work, attach the debugger and run the tests.
However there are now two launch configurations that allow to do this:

- "Debug ERPL SQL Unit tests" will build the extension and run the SQL unit tests. (For details on this tests see below).
- "Debug ERPL CPP Unit tests" will build and debug the extension unit tests. (For details on this tests see below).


## ➜ Running the extension
To run the extension code, simply start the shell with `./build/release/duckdb`.

Now we can use the features from the extension directly in DuckDB. The template contains a single scalar function `quack()` that takes a string arguments and returns a string:
```
D select quack('Jane') as result;
┌───────────────┐
│    result     │
│    varchar    │
├───────────────┤
│ Quack Jane 🐥 │
└───────────────┘
```

## ✔ Running the tests
Different tests can be created for DuckDB extensions. The primary way of testing DuckDB extensions should be the SQL tests in `./test/sql`. These SQL tests can be run using:
```sh
make test
```

## 📦 Deploying the extension (TODO)
To deploy the extension, simply run:
```sh ./scripts/extension_upload.sh```


## 💻 Installing the deployed binaries
To install your extension binaries from S3, you will need to do two things. Firstly, DuckDB should be launched with the 
`allow_unsigned_extensions` option set to true. How to set this will depend on the client you're using. Some examples:

CLI:
```shell
duckdb -unsigned
```

Python:
```python
con = duckdb.connect(':memory:', config={'allow_unsigned_extensions' : 'true'})
```

NodeJS:
```js
db = new duckdb.Database(':memory:', {"allow_unsigned_extensions": "true"});
```

Secondly, you will need to set the repository endpoint in DuckDB to the HTTP url of your bucket + version of the extension 
you want to install. To do this run the following SQL query in DuckDB:
```sql
SET custom_extension_repository='bucket.s3.eu-west-1.amazonaws.com/<your_extension_name>/latest';
```
Note that the `/latest` path will allow you to install the latest extension version available for your current version of 
DuckDB. To specify a specific version, you can pass the version instead.

After running these steps, you can install and load your extension using the regular INSTALL/LOAD commands in DuckDB:
```sql
INSTALL <your_extension_name>
LOAD <your_extension_name>
```

### Versioning of your extension
Extension binaries will only work for the specific DuckDB version they were built for. Since you may want to support multiple 
versions of DuckDB for a release of your extension, you can specify which versions to build for in the CI of this template.
By default, the CI will build your extension against the version of the DuckDB submodule, which should generally be the most
recent version of DuckDB. To build for multiple versions of DuckDB, simply add the version to the matrix variable, e.g.:
```
strategy:
    matrix:
        duckdb_version: [ '<submodule_version>', 'v0.7.0']
```

