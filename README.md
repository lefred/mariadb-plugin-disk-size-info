# mariadb-plugin-disk-size-info

![mariabd-plugin-disk-size-info](logo/disk-size-info.png)


`disk_size_info` is a MariaDB Server `INFORMATION_SCHEMA` plugin based on
[mysql-component-disksize](https://github.com/lefred/mysql-component-disksize).
It reports the free and total filesystem capacity for directories used by MariaDB Server.

## Build

Add this directory below `plugin/` in a MariaDB Server source tree (or link it there), configure MariaDB with `-DPLUGIN_DISK_SIZE_INFO=DYNAMIC`, and build the
`disk_size_info` target.

## Installation and use

```sql
INSTALL SONAME 'disk_size_info';

SELECT plugin_name, plugin_type, plugin_library, plugin_description, plugin_author 
FROM information_schema.PLUGINS WHERE plugin_library LIKE 'disk_size_info.so'\G
*************************** 1. row ***************************
       plugin_name: DISK_SIZE_INFO
       plugin_type: INFORMATION SCHEMA
    plugin_library: disk_size_info.so
plugin_description: Disk capacity for MariaDB server directories
     plugin_author: lefred (Frédéric Descamps)

SELECT *
FROM information_schema.DISK_SIZE_INFO;
```

The table contains:

| Column | Meaning |
| --- | --- |
| `DIR_NAME` | Directory inspected with `statvfs()` |
| `RELATED_VARIABLE` | MariaDB system variable that supplied the path |
| `FREE_SIZE` | Bytes available to an unprivileged process |
| `TOTAL_SIZE` | Total filesystem size in bytes |

The plugin checks MariaDB's data directory, temporary directories, binary and relay log locations, general/slow/error logs, replication metadata, and the Aria and InnoDB storage locations. It also recognizes paths exposed by Galera, RocksDB, DuckDB, the server-audit and SQL-error-log plugins, and AWS key
management when those components are installed.

File-valued variables are converted to their parent directories. Relative paths are resolved against `datadir`, multiple `tmpdir` entries and InnoDB data-file specifications are parsed, and duplicate variable/directory pairs are suppressed. Variables unavailable in the running server and paths that
cannot be inspected are skipped.

As filesystem and server path information can be sensitive, rows are returned only to users with the global `FILE` privilege, following MariaDB's existing `INFORMATION_SCHEMA.DISKS` plugin.

```sql
UNINSTALL SONAME 'disk_size_info';
```

### Example

```sql
SELECT dir_name, 
 json_pretty(related_variables) related_variables,
 format_bytes(free_size) free, format_bytes(total_size) total,
 concat(round(free_size/total_size*100,2), "%") used 
FROM information_schema.DISK_SIZE_INFO\G
*************************** 1. row ***************************
        dir_name: /mysql-test/var/log
related_variables: ["log_error"]
            free: 1.50 TiB
           total: 1.82 TiB
            used: 82.28%
*************************** 2. row ***************************
        dir_name: /mysql-test/var/mysqld.1
related_variables: [
    "general_log_file",
    "slow_query_log_file"
]
            free: 1.50 TiB
           total: 1.82 TiB
            used: 82.28%
*************************** 3. row ***************************
        dir_name: /mysql-test/var/mysqld.1/data
related_variables: [
    "datadir",
    "relay_log",
    "relay_log_basename",
    "relay_log_index",
    "relay_log_info_file",
    "master_info_file",
    "aria_log_dir_path",
    "innodb_log_group_home_dir",
    "innodb_undo_directory",
    "innodb_buffer_pool_filename",
    "innodb_data_file_path",
    "innodb_temp_data_file_path",
    "wsrep_data_home_dir"
]
            free: 1.50 TiB
           total: 1.82 TiB
            used: 82.28%
*************************** 4. row ***************************
        dir_name: /mysql-test/var/tmp/mysqld.1
related_variables: [
    "tmpdir",
    "slave_load_tmpdir"
]
            free: 1.50 TiB
           total: 1.82 TiB
            used: 82.28%
4 rows in set (0.000 sec)
```