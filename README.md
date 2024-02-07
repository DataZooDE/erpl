<a name="top"></a>

# Let your DuckDB quack SAP!

With ERPL start using Enterprise Data in your Data Science and ML pipelines within minutes!

## Quick Install

Install with two lines of code in DuckDB (unsigned option must be set):

```
SET custom_extension_repository = 'http://get.erpl.io';
INSTALL erpl;
```


## Quicklinks

- [★ What is the Erpl Extension?](#-what-is-the-erpl-extension) 
- [⚙ Example Usage](#-example-usage) 
- [➜ Obtaining the ERPL Extension](#-obtaining-the-erpl-extension)
- [💻 Installing the ERPL Binaries](#-installing-the-erpl-binaries)
- [Tracking](#tracking)
- [Licence](#license)

---

# ★ What is the ERPL Extension?
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

[Back to Top](#top)

## ⚙ Example Usage
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

This example can also be found in its [Python](./examples/flight_demo_python.ipynb), [R](./examples/flight_demo_r.ipynb), or [NODEJS](./examples/flight_demo_node.ipynb) version in the `examples` folder.

[Back to Top](#top)

## ➜ Obtaining the ERPL Extension

### Introduction
Building extensions for DuckDB can be challenging due to the varying C++ compiler and library ecosystem. This variability often leads to incompatibilities between locally built extensions and the centrally distributed DuckDB binary, primarily due to differences in the Application Binary Interface (ABI).

### Recommended Approach
To ensure compatibility and ease of use, we follow a build process similar to that of the DuckDB team. **Our advice is to start with the pre-compiled binaries available in our [GitHub releases](https://github.com/DataZooDE/erpl/releases).** For those interested in building the extension themselves, our [development instructions](./DEVELOPMENT.md) provide detailed guidance.

### Binary Selection
The assets in each release follow this naming convention:
```
erpl-${DUCKDB_VERSION}-extension-{OS}-{ARCH}.tar.gz
```
Choose the binary that matches your usage scenario. The table below summarizes the available binaries for various platforms and use cases:

| Usage       | Operating System | Architecture |
|-------------|------------------|--------------|
| DuckDB CLI  | Linux            | amd64_gcc4   |
| Python      | Linux            | amd64        |
| R           | Linux            | amd64        |
| Julia       | Linux            | amd64        |
| Node.js     | Linux            | amd64        |
| DuckDB CLI  | Windows          | amd64        |
| Python      | Windows          | amd64        |
| R           | Windows          | amd64        |
| Julia       | Windows          | amd64        |
| Node.js     | Windows          | amd64        |

### Note on OSX Support
Currently, we do not support OSX due to the unavailability of the SAP Netweaver RFC SDK for Apple Silicon. However, efforts are underway to enable compatibility via the x86 emulation layer, Rosetta.

This revised section aims to provide a clearer, more user-friendly explanation of how to acquire and choose the appropriate ERPL extension, along with a straightforward guide for those interested in building the extension themselves.

[Back to Top](#top)

## 💻 Installing the ERPL Binaries

### Introduction
Installation of the ERPL extension is straightforward. Please note that this extension is independent of the [DuckDB Foundation](https://duckdb.org/foundation/) and [DuckDB Labs](https://duckdblabs.com/), meaning the binaries are unsigned. Consequently, DuckDB must be initiated with the `-unsigned` flag. Detailed instructions on this process can be found in the [DuckDB documentation](https://duckdb.org/docs/archive/0.9.2/extensions/overview#ensuring-the-integrity-of-extensions).

### Installation Steps
1. **Enable Unsigned Extensions in DuckDB**: Set the `-unsigned` flag as described in the DuckDB documentation.
2. **Install and Load the ERPL Extension**:
   ```sql
   INSTALL 'path/to/erpl.duckdb_extension';
   LOAD 'erpl';
   ```

### Confirmation of Successful Installation
Upon successful installation and loading, the extension will output the following message:
```
-- Loading ERPL Trampoline Extension. --
(Saves ERPL SAP dependencies to '/home/jr/.duckdb/extensions/v0.9.2/linux_amd64' and loads them)
ERPL extension saved and loaded from /home/jr/.duckdb/extensions/v0.9.2/linux_amd64/erpl_impl.duckdb_extension.
For usage instructions, visit https://erpl.io
```

### Understanding the Extension Loading Process
The ERPL extension is composed of two parts:
1. **Trampoline Extension**: Extracts SAP Netweaver RFC SDK and SAP Business Warehouse BICS libraries from the binary.
2. **Implementation Extension**: The actual functional part of the extension.

The `erpl_init` function in the trampoline extension bundles and extracts dependencies into the DuckDB extension folder. Post-installation, the directory `~/.duckdb/extensions/v0.9.2/linux_amd64` should contain the following files:
```
-rw-r--r-- 1 jr jr 110M 26. Nov 10:23 erpl.duckdb_extension
-rw-r--r-- 1 jr jr  34M 26. Nov 10:35 erpl_impl.duckdb_extension
-rw-r--r-- 1 jr jr  20M 26. Nov 10:35 libicudata.so.50
-rw-r--r-- 1 jr jr  12M 26. Nov 10:35 libicui18n.so.50
-rw-r--r-- 1 jr jr 8,4M 26. Nov 10:35 libicuuc.so.50
-rw-r--r-- 1 jr jr 9,5M 26. Nov 10:35 libsapnwrfc.so
-rw-r--r-- 1 jr jr 1,1M 26. Nov 10:35 libsapucum.so
```

This revised section aims for a clearer, more structured presentation of the installation process, ensuring users can easily understand and follow the steps.

[Back to Top](#top)

## Tracking

### Overview
Our extension automatically collects basic usage data to enhance its performance and gain insights into user engagement. We employ [Posthog](https://posthog.com/) for data analysis, transmitting information securely to the European Posthog instance at [https://eu.posthog.com](https://eu.posthog.com) via HTTPS.

### Data Collected
Each transmitted request includes the following information:
- Extension Version
- DuckDB Version
- Operating System
- System Architecture
- MAC Address of the Primary Network Interface

### Event Tracking
Data is transmitted under these circumstances:
- **Extension Load**: No extra data is sent beyond the initial usage information.
- **Function Invocation**: The name of the invoked function is sent. *Note: Function inputs/outputs are not transmitted.*
- **Error Occurrence**: The error message is transmitted.

### User Configuration Options
Users can control tracking through these settings:

1. **Enable/Disable Tracking**:
   ```sql
   SET erpl_telemetry_enabled = TRUE; -- Enabled by default; set to FALSE to disable tracking
   ```
   
2. **Posthog API Key Configuration** (usually unchanged):
   ```sql
   SET erpl_telemetry_key = 'phc_XXX'; -- Pre-set to our key; customizable to your own key
   ```

This approach ensures transparency about data collection while offering users control over their privacy settings.

[Back to Top](#top)

## License
The ERPL extension is licensed under the [Business Source License (BSL) Version 1.1](./LICENSE.md). The BSL is a source-available license that gives you the following permissions:

### Allowed:
1. **Copy, Modify, and Create Derivative Works**: You have the right to copy the software, modify it, and create derivative works based on it.
2. **Redistribute and Non-Production Use**: Redistribution and non-production use of the software is permitted.
3. **Limited Production Use**: You can make production use of the software, but with limitations. Specifically, the software cannot be offered to third parties on a hosted or embedded basis.
4. **Change License Rights**: After the Change Date (five years from the first publication of the Licensed Work), you gain rights under the terms of the Change License (MPL 2.0). This implies broader permissions after this date.

### Not Allowed:
1. **Offering to Third Parties on Hosted/Embedded Basis**: The Additional Use Grant restricts using the software in a manner that it is offered to third parties on a hosted or embedded basis.
2. **Violation of Current License Requirements**: If your use does not comply with the requirements of the BSL, you must either purchase a commercial license or refrain from using the software.
3. **Trademark Usage**: You don't have rights to any trademark or logo of Licensor or its affiliates, except as expressly required by the License.

### Additional Points:
- **Separate Application for Each Version**: The license applies individually to each version of the Licensed Work. The Change Date may vary for each version.
- **Display of License**: You must conspicuously display this License on each original or modified copy of the Licensed Work.
- **Third-Party Receipt**: If you receive the Licensed Work from a third party, the terms and conditions of this License still apply.
- **Automatic Termination on Violation**: Any use of the Licensed Work in violation of this License automatically terminates your rights under this License for all versions of the Licensed Work.
- **Disclaimer of Warranties**: The Licensed Work is provided "as is" without any warranties, including but not limited to the warranties of merchantability, fitness for a particular purpose, non-infringement, and title.

This summary is based on the provided license text and should be used as a guideline. For legal advice or clarification on specific points, consulting a legal professional is recommended, especially for commercial or complex use cases.

[Back to Top](#top)
