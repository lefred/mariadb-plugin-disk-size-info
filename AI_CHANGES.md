# AI-Assisted Changes

This file logs changes made to this repository with AI assistance, for
transparency.

## 2026-07-27 — GPT-5 (Codex)

- Changed `DISK_SIZE_INFO` to return one row per resolved directory instead of
  one row per variable/directory pair.
- Replaced `RELATED_VARIABLE` with `RELATED_VARIABLES`, containing a JSON array
  of every system variable that resolves to the directory.
- A directory is now inspected with `statvfs()` only once per query.
- Updated the documentation and tests for the grouped result.

## 2026-07-27 — Claude Sonnet 5 (claude-sonnet-5)

Code review of `disk_size_info.cc` followed by fixes for the issues found:

- **Fix:** `innodb_data_file_path` and `innodb_temp_data_file_path` entries
  without an absolute path are resolved by InnoDB relative to
  `innodb_data_home_dir` (which itself defaults to `datadir`), not directly
  relative to `datadir`. The plugin previously resolved every relative
  `INNODB_FILE_LIST` path against `datadir` unconditionally, which reports
  the wrong filesystem when `innodb_data_home_dir` is configured to a
  different location than `datadir`. `disk_size_info_fill_table()` now
  resolves `innodb_data_home_dir` once (falling back to `datadir` when
  unset) and uses it as the base directory for `innodb_data_file_path` /
  `innodb_temp_data_file_path` entries.
- **Fix:** Corrected a misleading comment above the `slow_query_log_file`
  entry in `path_variables` that had the alias relationship backwards; it
  now just notes that `slow_query_log_file` and `log_slow_query_file` share
  the same underlying storage, so only one is listed to avoid a duplicate
  row.

No behavior changes to privilege checks, schema, or the general path
resolution/dedup logic — verified those against MariaDB core
(`find_sys_var`/`val_str` locking, `check_global_access(FILE_ACL)` usage in
`plugin/disks`, and InnoDB's `srv_data_home`/`SysTablespace::set_path()`
computation in `ha_innodb.cc`).
