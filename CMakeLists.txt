# Copyright (c) 2019,2024,2025,2026 MariaDB Corporation
# Copyright (c) 2026 lefred (Frédéric Descamps)
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.

cmake_minimum_required(VERSION 3.12)

project(type_email)

set(PLUGIN_AUTHOR "lefred")
set(PLUGIN_VERSION "0.2.2")
set(PLUGIN_DESCRIPTION "Disk Size Info")

set(TYPE_PLUGIN_SOURCES
    disk_size_info.cc
)

find_package(mariadb-plugin QUIET)

if(COMMAND MARIADB_ADD_PLUGIN)

    MARIADB_ADD_PLUGIN(disk_size_info
        ${TYPE_PLUGIN_SOURCES}
        MODULE_ONLY
        RECOMPILE_FOR_EMBEDDED
        AUTHOR "${PLUGIN_AUTHOR}"
        VERSION "${PLUGIN_VERSION}"
        DESCRIPTION "${PLUGIN_DESCRIPTION}"
    )

elseif(COMMAND MYSQL_ADD_PLUGIN)

    MYSQL_ADD_PLUGIN(disk_size_info
        ${TYPE_PLUGIN_SOURCES}
        MODULE_ONLY
        RECOMPILE_FOR_EMBEDDED
        VERSION "${PLUGIN_VERSION}"
    )

    target_compile_definitions(type_email PRIVATE
        PLUGIN_AUTHOR="${PLUGIN_AUTHOR}"
        PLUGIN_DESCRIPTION="${PLUGIN_DESCRIPTION}"
    )

endif()

include(CPack)
