vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Jadm1992/Grape
    REF v2.0.0
    SHA512 0 # You will need to replace this with the actual SHA512 of the v2.0.0 release archive
    HEAD_REF main
)

file(INSTALL "${SOURCE_PATH}/include/grape.h" DESTINATION "${CURRENT_PACKAGES_DIR}/include")
file(INSTALL "${SOURCE_PATH}/LICENSE" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
