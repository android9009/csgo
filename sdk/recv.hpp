#pragma once
// Source 1 networking metadata (RecvTable/RecvProp). Used by PropManager, not Source 2 schema.
#include <cstdint>

enum SendPropType : int {
    DPT_Int = 0, DPT_Float, DPT_Vector, DPT_VectorXY, DPT_String,
    DPT_Array, DPT_DataTable, DPT_Int64
};
struct RecvTable;
struct RecvProp {
    const char* m_pVarName;
    SendPropType m_RecvType;
    int m_Flags;
    int m_StringBufferSize;
    bool m_bInsideArray;
    const void* m_pExtraData;
    RecvProp* m_pArrayProp;
    void* m_ArrayLengthProxy;
    void* m_ProxyFn;
    void* m_DataTableProxyFn;
    RecvTable* m_pDataTable;
    int m_Offset;
    int m_ElementStride;
    int m_nElements;
    const char* m_pParentArrayPropName;
};
struct RecvTable {
    RecvProp* m_pProps;
    int m_nProps;
    void* m_pDecoder;
    const char* m_pNetTableName;
    bool m_bInitialized;
    bool m_bInMainList;
};
