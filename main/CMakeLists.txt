set(include_dirs
    ".")

idf_component_register(SRCS         "main.c"
                       INCLUDE_DIRS "${include_dirs}")
