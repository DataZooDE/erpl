# SAP BICS Describe InfoProvider

As example I selected the query `0D_FC_NW_C01_Q0007`, name of the infoProvider is `0D_FC_NW_C01`.

## Protocol steps

| Step No. | Function | Input | Output |
|---------|----------|-------|--------|
|    1    | BICS_CONS_CREATE_DATA_AREA | No input | E_APPLICATION_HANDLE (e.g 0001) |
|    2    | BICS_PROV_OPEN | Application Handle (0001), Data Provider Name (0D_FC_NW_C01_Q0007), State Variable Mode (U), Optimize Init Version(5) | Data provider handle (e.g 0005), conatiner handle (4) |
|    3    | BICS_PROV_GET_DESIGN_TIME_INFO | Data provider handle (0005) | Huge export of E_SX_META_DATA,  |


