idf_component_register(
    SRC_DIRS "src"
    INCLUDE_DIRS "src"
)

# TinyCBOR expects the standard math library for float encode/decode helpers.
target_link_libraries(${COMPONENT_LIB} PRIVATE m)
