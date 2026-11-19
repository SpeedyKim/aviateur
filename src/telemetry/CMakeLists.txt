file(GLOB TELEMETRY_SRC_LIST
        *.cpp
        *.h
)

target_sources(${PROJECT_NAME} PRIVATE ${TELEMETRY_SRC_LIST})
