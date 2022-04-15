AUX_SOURCE_DIRECTORY(. config_test_srcs)
add_executable(config_test ${config_test_srcs} ${PROJECT_SOURCE_DIR}/appconfig/appconfig.h ${PROJECT_SOURCE_DIR}/appconfig/appconfig.c)
target_link_libraries(config_test xmonitor-utils libconfig.a libzlog.a pthread z m)