# Copyright (c) 2019,2024,2025,2026 MariaDB Corporation
# Copyright (c) 2026 lefred (Frédéric Descamps)
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; version 2 of the License.

MYSQL_ADD_PLUGIN(disk_size_info disk_size_info.cc MODULE_ONLY
                 RECOMPILE_FOR_EMBEDDED)
