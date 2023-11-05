# ★ What is the Erpl Extension?
The primary objective of this [DuckDB](https://duckdb.org) extension is to facilitate seamless integration with the SAP data ecosystem. Our approach prioritizes:
- **Minimal dependencies**: Ensuring a lightweight experience.
- **User-centric design**: Intuitive and straightforward usage for DuckDB users.

We focus predominantly on two main use cases:
- **Data Science & Analytics**: Directly accessing data from SAP ERP (via RFC) and SAP BW (via BICS) for interactive analytical tasks.
- **Data Replication**: Efficiently migrating data from SAP ERP or SAP BW into DuckDB for incorporation into DuckDB-centric data workflows.

Please be aware that DataZoo GmbH is the independent developer of this extension and does not hold any affiliation with DuckDB Labs or the DuckDB Foundation. "DuckDB" is a trademark registered by the DuckDB Foundation.

Our development journey is underway, with a functioning prototype available that facilitates:
- Data queries from SAP ERP tables.
- Execution of RFC functions.

Transparency is our ethos, and in line with this, we are planning a commercial trajectory for the extension, structured as follows:
- **Community Edition**: A gratis version for individual users, both private and commercial, enabling queries from SAP ERP tables.
- **Commercial License**: Designed for businesses developing services or products leveraging our extension. More details can be found in [our BSL v1. License](./LICENSE.md).
- **Enterprise Edition**: A premium version offering additional capabilities to query data from SAP Business Warehouse and data replication via SAP ODP.

For inquiries, potential collaborations, or if your curiosity is piqued, connect with us at [https://erpl.io](https://erpl.io).

## ➜ Example Usage
A first example demonstrates how to join two SAP tables with an external table. We’ll be using the [ABAP Flight Reference Scenario](https://help.sap.com/docs/ABAP_PLATFORM_NEW/fc4c71aa50014fd1b43721701471913d/a9d7c7c140a0408dbc5966c52d156b49.html), specifically joining the `SFLIGHT` and `SPFLI` tables which contain flight and flight schedule details respectively, with an external table `WEATHER` that holds weather information. We will extract flight information and associated temperatures at departure and arrival cities.

To start with, we assume you have setup DuckDB and already installed the ERPL extension (see below) and have access to an SAP system having the ABAP Flight Reference Scenario data. 

First we start DuckDB with the `-unsigned` - flag set. Then we load the extension and configure the necessary credentials to connect to the SAP system:
```sql
LOAD `erpl`;

SET sap_ashost = 'localhost';
SET sap_sysnr = '00';
SET sap_user = 'DEVELOPER';
SET sap_password = 'Htods70334';
SET sap_client = '001';
SET sap_lang = 'EN';
```
In our case we use the docker based [ABAP Platform Trial](https://hub.docker.com/r/sapse/abap-platform-trial). The credentials are set by default, details can be found in the documentation of the docker image. 

Now we explore the schema of the three tables tables:
<details>
<summary>
SELECT * FROM SAP_DESCRIBE_FIELDS('SFLIGHT');
</summary>

|   pos | is_key   | field      | text                               | sap_type   |   length |   decimals | check_table   | ref_table   | ref_field   | language   |
|------:|:---------|:-----------|:-----------------------------------|:-----------|---------:|-----------:|:--------------|:------------|:------------|:-----------|
|  0001 | X        | MANDT      | Client                             | CLNT       |   000003 |     000000 | T000          |             |             | E          |
|  0002 | X        | CARRID     | Airline Code                       | CHAR       |   000003 |     000000 | SCARR         |             |             | E          |
|  0003 | X        | CONNID     | Flight Connection Number           | NUMC       |   000004 |     000000 | SPFLI         |             |             | E          |
|  0004 | X        | FLDATE     | Flight date                        | DATS       |   000008 |     000000 |               |             |             | E          |
|  0005 |          | PRICE      | Airfare                            | CURR       |   000015 |     000002 |               | SFLIGHT     | CURRENCY    | E          |
|  0006 |          | CURRENCY   | Local currency of airline          | CUKY       |   000005 |     000000 | SCURX         |             |             | E          |
|  0007 |          | PLANETYPE  | Aircraft Type                      | CHAR       |   000010 |     000000 | SAPLANE       |             |             | E          |
|  0008 |          | SEATSMAX   | Maximum Capacity in Economy Class  | INT4       |   000010 |     000000 |               |             |             | E          |
|  0009 |          | SEATSOCC   | Occupied Seats in Economy Class    | INT4       |   000010 |     000000 |               |             |             | E          |
|  0010 |          | PAYMENTSUM | Total of current bookings          | CURR       |   000017 |     000002 |               | SFLIGHT     | CURRENCY    | E          |
|  0011 |          | SEATSMAX_B | Maximum Capacity in Business Class | INT4       |   000010 |     000000 |               |             |             | E          |
|  0012 |          | SEATSOCC_B | Occupied Seats in Business Class   | INT4       |   000010 |     000000 |               |             |             | E          |
|  0013 |          | SEATSMAX_F | Maximum Capacity in First Class    | INT4       |   000010 |     000000 |               |             |             | E          |
|  0014 |          | SEATSOCC_F | Occupied Seats in First Class      | INT4       |   000010 |     000000 |               |             |             | E          |

</details>

<details>
<summary>
SELECT * FROM SAP_DESCRIBE_FIELDS('SPFLI');
</summary>

|   pos | is_key   | field     | text                               | sap_type   |   length |   decimals | check_table   | ref_table   | ref_field   | language   |
|------:|:---------|:----------|:-----------------------------------|:-----------|---------:|-----------:|:--------------|:------------|:------------|:-----------|
|  0001 | X        | MANDT     | Client                             | CLNT       |   000003 |     000000 | T000          |             |             | E          |
|  0002 | X        | CARRID    | Airline Code                       | CHAR       |   000003 |     000000 | SCARR         |             |             | E          |
|  0003 | X        | CONNID    | Flight Connection Number           | NUMC       |   000004 |     000000 |               |             |             | E          |
|  0004 |          | COUNTRYFR | Country Key                        | CHAR       |   000003 |     000000 | SGEOCITY      |             |             | E          |
|  0005 |          | CITYFROM  | Departure city                     | CHAR       |   000020 |     000000 | SGEOCITY      |             |             | E          |
|  0006 |          | AIRPFROM  | Departure airport                  | CHAR       |   000003 |     000000 | SAIRPORT      |             |             | E          |
|  0007 |          | COUNTRYTO | Country Key                        | CHAR       |   000003 |     000000 | SGEOCITY      |             |             | E          |
|  0008 |          | CITYTO    | Arrival city                       | CHAR       |   000020 |     000000 | SGEOCITY      |             |             | E          |
|  0009 |          | AIRPTO    | Destination airport                | CHAR       |   000003 |     000000 | SAIRPORT      |             |             | E          |
|  0010 |          | FLTIME    | Flight time                        | INT4       |   000010 |     000000 |               |             |             | E          |
|  0011 |          | DEPTIME   | Departure time                     | TIMS       |   000006 |     000000 |               |             |             | E          |
|  0012 |          | ARRTIME   | Arrival time                       | TIMS       |   000006 |     000000 |               |             |             | E          |
|  0013 |          | DISTANCE  | Distance                           | QUAN       |   000009 |     000004 |               | SPFLI       | DISTID      | E          |
|  0014 |          | DISTID    | Mass unit of distance (kms, miles) | UNIT       |   000003 |     000000 |               |             |             | E          |
|  0015 |          | FLTYPE    | Flight type                        | CHAR       |   000001 |     000000 |               |             |             | E          |
|  0016 |          | PERIOD    | Arrival n day(s) later             | INT1       |   000003 |     000000 |               |             |             | E          |

</details>

<details>
<summary>
DESCRIBE SELECT * FROM 'WEATHER.csv';
</summary>

| column_name   | column_type   | null   | key   | default   | extra   |
|:--------------|:--------------|:-------|:------|:----------|:--------|
| FLDATE        | DATE          | YES    |       |           |         |
| COUNTRY       | VARCHAR       | YES    |       |           |         |
| CITY          | VARCHAR       | YES    |       |           |         |
| TEMPERATURE   | DOUBLE        | YES    |       |           |         |
| CONDITION     | VARCHAR       | YES    |       |           |         |

</details>

The actual example joins this three tables:
```sql
SELECT 
  f.CARRID,
  f.CONNID,
  f.FLDATE,
  s.CITYFROM as CITY_FROM,
  ROUND(w_from.TEMPERATURE, 1) as TEMP_FROM,
  s.CITYTO as CITY_TO,
  ROUND(w_to.TEMPERATURE, 1) as TEMP_TO,
  FROM sap_read_table('SFLIGHT') AS f
  JOIN sap_read_table('SPFLI') AS s 
      ON (f.MANDT = s.MANDT AND f.CARRID = s.CARRID AND f.CONNID = s.CONNID)
  JOIN "WEATHER.csv" AS w_from
      ON (f.FLDATE = w_from.FLDATE AND s.COUNTRYFR = w_from.COUNTRY AND s.CITYFROM = w_from.CITY)
  JOIN "WEATHER.csv" AS w_to
      ON (f.FLDATE = w_to.FLDATE AND s.COUNTRYTO = w_to.COUNTRY AND s.CITYTO = w_to.CITY)
  ORDER BY 1, 2, 3
  LIMIT 25
```

This SQL query performs the following operations:
- Retrieves flight details from `SFLIGHT` using ERPL's `sap_read_table`, aliasing it as `f`.
- Again using ERPL's `sap_read_table` we join `SPFLI` (aliased as `s`) on `MANDT`, `CARRID`, and `CONNID` to get the flight's city of origin and destination.
- Incorporates two instances of an external weather data CSV file, `w_from` and `w_to`, matching on flight date and respective cities' country and name for departure and arrival.
- Rounds the temperature data to one decimal place for readability.
- Orders the results by `CARRIER_ID`, `CONNECTION_ID`, and `FLIGHT_DATE`.
- Limits the output to the first 25 rows for a concise view.

The output of this query will provide a comprehensive view of the flights, including their departure and arrival cities, and the corresponding temperatures, thus offering valuable insights for flight operations analysis.

```
| CARRID   |   CONNID | FLDATE              | CITY_FROM   |   TEMP_FROM | CITY_TO       |   TEMP_TO |
|:---------|---------:|:--------------------|:------------|------------:|:--------------|----------:|
| AA       |     0017 | 2016-11-15 00:00:00 | NEW YORK    |        28.3 | SAN FRANCISCO |      19.8 |
| AA       |     0017 | 2017-02-03 00:00:00 | NEW YORK    |        18.9 | SAN FRANCISCO |      17.2 |
| AA       |     0017 | 2017-04-24 00:00:00 | NEW YORK    |        14.7 | SAN FRANCISCO |      16.2 |
| AA       |     0017 | 2017-07-13 00:00:00 | NEW YORK    |        16.8 | SAN FRANCISCO |      22.5 |
| AA       |     0017 | 2017-10-01 00:00:00 | NEW YORK    |        14.6 | SAN FRANCISCO |      28.3 |
| AA       |     0017 | 2017-12-20 00:00:00 | NEW YORK    |        13   | SAN FRANCISCO |      21.4 |
| AZ       |     0555 | 2016-11-15 00:00:00 | ROME        |        20.6 | FRANKFURT     |      24.2 |
| AZ       |     0555 | 2017-02-03 00:00:00 | ROME        |        13.1 | FRANKFURT     |      24.1 |
| AZ       |     0555 | 2017-04-24 00:00:00 | ROME        |        20.7 | FRANKFURT     |      24.6 |
| AZ       |     0555 | 2017-07-13 00:00:00 | ROME        |        14   | FRANKFURT     |      16.3 |
| AZ       |     0555 | 2017-10-01 00:00:00 | ROME        |        20.9 | FRANKFURT     |      15.3 |
| AZ       |     0555 | 2017-12-20 00:00:00 | ROME        |        23.8 | FRANKFURT     |      24.7 |
| AZ       |     0789 | 2016-11-15 00:00:00 | TOKYO       |        29.7 | ROME          |      20.6 |
| AZ       |     0789 | 2017-02-03 00:00:00 | TOKYO       |        23.4 | ROME          |      13.1 |
| AZ       |     0789 | 2017-04-24 00:00:00 | TOKYO       |        28.5 | ROME          |      20.7 |
| AZ       |     0789 | 2017-07-13 00:00:00 | TOKYO       |        19.4 | ROME          |      14   |
| AZ       |     0789 | 2017-10-01 00:00:00 | TOKYO       |        19.8 | ROME          |      20.9 |
| AZ       |     0789 | 2017-12-20 00:00:00 | TOKYO       |        15.6 | ROME          |      23.8 |
| DL       |     0106 | 2016-11-13 00:00:00 | NEW YORK    |         9.1 | FRANKFURT     |      13.8 |
| DL       |     0106 | 2017-02-01 00:00:00 | NEW YORK    |        24.3 | FRANKFURT     |      23.9 |
| DL       |     0106 | 2017-04-22 00:00:00 | NEW YORK    |        15.8 | FRANKFURT     |      23.5 |
| DL       |     0106 | 2017-07-11 00:00:00 | NEW YORK    |        20.7 | FRANKFURT     |      30.6 |
| DL       |     0106 | 2017-09-29 00:00:00 | NEW YORK    |        16.3 | FRANKFURT     |      19.1 |
| DL       |     0106 | 2017-12-18 00:00:00 | NEW YORK    |        14.4 | FRANKFURT     |      21.8 |
| JL       |     0407 | 2016-11-17 00:00:00 | TOKYO       |        20.3 | FRANKFURT     |      20.4 |
```

This example can also be found in it's [Python version in the `examples` folder](./examples/flight_example_python.ipynb).

# How do I get it?
Currently building extensions for DuckDB is not as easy as it should be. It is quite easily feasable to build the extension locally, however due to instability in the application binary interface (ABI) dependent on the build environment, often time the extension and the centrally distributed DuckDB binary are not compatible. 

To overcome this issue, we use a build system very similar to the one used by the DuckDB team. **We recommend to start with our binaries available here from GitHub releases.**

## Installing the ERPL binaries.
First step to getting started is to clone this template onto your local computer. Then clone your new repository using 
```sh
git clone --recurse-submodules https://bitbucket.org/erpel/erpel.git
```
Note that `--recurse-submodules` will ensure the correct version of duckdb is pulled. This is necessary to make the DuckDB based extension build system work.
Another dependency is the SAP Netweaver RFC SDK. This is not included in the template, but can be downloaded from the SAP Service Marketplace. The resulting 
zip file should be placed in the `./nwrfcsdk` folder. The build system will automatically detect the SDK and build the extension against it.

## Installing as python package
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


## ⚙ Running the extension
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

