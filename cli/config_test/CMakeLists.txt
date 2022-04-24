AUX_SOURCE_DIRECTORY(. config_test_srcs)
add_executable(config_test ${config_test_srcs} 
                            ${PROJECT_SOURCE_DIR}/appconfig/appconfig.h 
                            ${PROJECT_SOURCE_DIR}/appconfig/appconfig.c
                            ${PROJECT_SOURCE_DIR}/collectors/application/apps_filter_rule.h
                            ${PROJECT_SOURCE_DIR}/collectors/application/apps_filter_rule.c)
target_link_libraries(config_test xmonitor-utils libpcre2-8.a libconfig.a libzlog.a pthread z m)