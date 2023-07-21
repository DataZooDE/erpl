include_directories(include)

add_library(yyjson_erpel OBJECT yyjson.cpp)
set(YYJSON_OBJECT_FILES
    ${YYJSON_OBJECT_FILES} $<TARGET_OBJECTS:yyjson_erpel>
    PARENT_SCOPE)

disable_target_warnings(yyjson_erpel)
