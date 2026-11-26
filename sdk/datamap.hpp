#pragma once
#include <cstddef>
#include <cstdint>
enum typedescription_t_FieldType : int {
    FIELD_VOID = 0, FIELD_FLOAT, FIELD_STRING, FIELD_VECTOR, FIELD_QUATERNION,
    FIELD_INTEGER, FIELD_BOOLEAN, FIELD_SHORT, FIELD_CHARACTER, FIELD_COLOR32,
    FIELD_EMBEDDED, FIELD_CUSTOM, FIELD_CLASSPTR, FIELD_EHANDLE, FIELD_EDICT,
    FIELD_POSITION_VECTOR, FIELD_TIME, FIELD_TICK, FIELD_MODELNAME, FIELD_SOUNDNAME,
    FIELD_INPUT, FIELD_FUNCTION, FIELD_VMATRIX, FIELD_VMATRIX_WORLDSPACE,
    FIELD_MATRIX3X4_WORLDSPACE, FIELD_INTERVAL, FIELD_MODELINDEX, FIELD_MATERIALINDEX,
    FIELD_VECTOR2D, FIELD_INTEGER64
};
struct datamap_t;
struct typedescription_t {
    typedescription_t_FieldType fieldType;
    const char* fieldName;
    int fieldOffset[2];
    unsigned short fieldSize;
    short flags;
    const char* externalName;
    void* saveRestoreOps;
    void* inputFunc;
    datamap_t* td;
    int fieldSizeInBytes;
    typedescription_t* overrideField;
    int overrideCount;
    float fieldTolerance;
};
struct datamap_t {
    typedescription_t* dataDesc;
    int dataNumFields;
    const char* dataClassName;
    datamap_t* baseMap;
    bool chainsValidated;
    bool packedOffsetsComputed;
    int packedSize;
};
