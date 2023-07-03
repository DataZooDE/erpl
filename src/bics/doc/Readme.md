# Preliminary documentation for ERPL BICS interface

ERPL BICS is a duckdb extension which allows to query SAP BW cubes and queries using the BICS interface.

## How to use ERPL BICS

The extension provides a simple SQL style API for generating BICS queries against SAP BW cube or query. 
ERPL does not provide APIs for all query scenarios, but it does cover the most use cases including slice, 
dice, drilldown, rollup, and pivot scenarios in N dimensions. The goal is to provide a simple and intuitive 
API for querying SAP BW cubes and queries for data analysis and data science.


### Workflow for query using ERPL BICS

1. Setup credentials via SQL `SET` commands or via `sapnwrfc.ini`.
2. Create a query handle using
   ```sql
    CALL bics_create_session();
    ```
    This returns you a session handle which you can use for all subsequent calls. The output of this should look like.
    ```
    session_id | session_alias      |
    ------------+-------------------+
            1  | albert_heisenberg  |
    (1 row)
    ```

3. Add dimensions and measures to the query handle using
    ```sql
     CALL bics_select_cube(1, 'ZADEMO_CUBE');
    ```

4. Add dimensions and measures to the query handle using
    ```sql
     CALL bics_add_column(1, '0KEY_FIGURE1');
     CALL bics_add_column(1, '0KEY_FIGURE2');
     CALL bics_add_row(1,    '0CHAR1');
    ```

5. Add filters to the query handle using
    ```sql
     CALL bics_add_filter(1, '0CHAR1', 'EQ', 'A');
    ```

6. Execute the query and return the result
    ```sql
     select * bics_invoke(1);
    ```
    This returns you a result set which you can use for all subsequent calls. The output of this should look like.
    ```
    0CHAR1 | 0KEY_FIGURE1 | 0KEY_FIGURE2 |
    --------+--------------+--------------+
    A      |           10 |           20 |
    B      |           30 |           40 |
    C      |           50 |           60 |
    D      |           70 |           80 |
    (4 rows)
    ```


### Workflow for exploration using ERPL BICS
You can use the explore function to return a list of cubes or queries, dimensions, or members to use in constructing your session. This is handy if you don't have access to other OLAP browsing tools, or if you want to programmatically manipulate or construct the session.

1. Setup credentials via SQL `SET` commands or via `sapnwrfc.ini`.

2. You can explore the available cubes and queries using
    ```sql
     select * from bics_explore(cube='*');
    ```
    This returns you a result set which you can use for all subsequent calls. The output of this should look like.
    ```
    NAME        | DESCRIPTION | TYPE | 
    ------------+-------------+------+
    ZADEMO_CUBE | ZADEMO_CUBE | CUBE |
    (1 row)
    ```
    or for a query
    ```sql
     select * from bics_explore(query='*');
    ```
    This returns you a result set which you can use for all subsequent calls. The output of this should look like.
    ```
    NAME        | DESCRIPTION | TYPE  |
    ------------+-------------+-------+
    ZADEMO_QUERY| ZADEMO_QUERY| QUERY |
    (1 row)
    ```

3. You can explore the available dimensions in a cube or query using
    ```sql
     select * from bics_explore(cube='ZADEMO_CUBE', dimension='*');
    ```
    This returns you a result set which you can use for all subsequent calls. The output of this should look like.
    ```
    NAME        | DESCRIPTION | TYPE      | LENGTH |
    ------------+-------------+-----------+--------|
    0CHAR1      | 0CHAR1      | CHARACTER |     10 |
    0CHAR2      | 0CHAR2      | CHARACTER |     20 |
    (2 rows)
    ```

4. You further drill in the hierchies down to the info-object
    ```sql
     select * from bics_explore(cube='ZADEMO_CUBE', dimension='0CHAR1', hierarchy='*');
    ```
    This returns you a result set which you can use for all subsequent calls. The output of this should look like (**This output does not make sense yet**).
    ```
    NAME        | DESCRIPTION | TYPE      | LENGTH |
    ------------+-------------+-----------+--------|
    0CHAR1      | 0CHAR1      | CHARACTER |     10 |
    0CHAR2      | 0CHAR2      | CHARACTER |     20 |
    (2 rows)
    ```

## The BICS interface
BICS, or BI Consumer Services, is an interface used for executing queries and fetching result data from SAP Business Warehouse (BW) systems. It comes in two forms: the "classic" BICS interface, which is accessible via RFC-enabled function modules and is used by various SAP clients like Analysis for Office, and the "modern" InA interface, which is HTTP-based and used by SAP Analytics Cloud for live connections. These interfaces allow external systems or ABAP code to access BW query data and handle complex tasks related to data analytics and data science​1​.

BICS is important for data analytics and data science because it provides robust access to the settings and properties of query navigation states, and even integrates planning features. It can be called remotely from outside the system, eliminating the need for additional adapter code. This makes it a powerful tool for handling data from SAP BW, despite its complex interface with hundreds of settings and properties​1​.

BICS function modules, which start with BICS_, are RFC enabled and can be executed directly from a remote connection. They offer a range of functions for general features and manipulating data providers. These function modules are used by standard SAP clients to access BW, and they can also be used by custom "clients" developed in ABAP or in an environment where RFCs can be called, like Java via JCo or .NET Connector​​.

Interpreting the result data fetched by BICS is not trivial, but it is manageable. The result data is returned in a multidimensional format, which can be interpreted and displayed based on the requirements of the application or user​​.

In terms of its relationship with MDX (Multidimensional Expressions), I could not find specific details, and it appears that more research is needed. MDX is a language used for querying and manipulating multidimensional data, and it is often used in the context of OLAP (Online Analytical Processing) databases, which include systems like SAP BW. While BICS does offer multidimensional data access, the specific interaction between BICS and MDX was not clearly outlined in the sources I was able to access. I can continue to research this topic if you'd like more information.

The BICS classes represent a runtime data model of BW applications and their components like queries and planning objects. These classes are located within the BW_BICS root package, which contains two subpackages: BW_BICS_BASE and BW_BICS_CONSUMER. These packages hold important general objects and "consumable" objects respectively​1​.

## References and Information
https://evosight.com/sap-bw-bics-interface