/* Copyright (c) 2019,2024,2025,2026 MariaDB Corporation
   Copyright (c) 2026 lefred (Frédéric Descamps)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1335  USA */

#define MYSQL_SERVER

#include <my_global.h>
#include <my_sys.h>
#include <mysql/plugin.h>
#include <sql_acl.h>
#include <sql_class.h>
#include <sql_i_s.h>
#include <set_var.h>

#ifndef _WIN32
#include <sys/statvfs.h>
#endif

#include <string>
#include <map>
#include <vector>

namespace {

enum Path_kind
{
  DIRECTORY_PATH,
  FILE_PATH,
  PATH_LIST,
  INNODB_FILE_LIST
};

struct Path_variable
{
  const char *name;
  Path_kind kind;
  bool empty_means_datadir;
};

static const Path_variable path_variables[]=
{
  /* Core server directories and files that can grow during operation. */
  {"datadir", DIRECTORY_PATH, false},
  {"tmpdir", PATH_LIST, false},
  {"binlog_directory", DIRECTORY_PATH, false},
  {"log_bin_basename", FILE_PATH, false},
  {"log_bin_index", FILE_PATH, false},
  {"relay_log", FILE_PATH, false},
  {"relay_log_basename", FILE_PATH, false},
  {"relay_log_index", FILE_PATH, false},
  {"relay_log_info_file", FILE_PATH, false},
  {"master_info_file", FILE_PATH, false},
  {"slave_load_tmpdir", DIRECTORY_PATH, false},
  {"general_log_file", FILE_PATH, false},
  /*
    slow_query_log_file and log_slow_query_file share the same underlying
    storage; listing both would produce a duplicate row.
  */
  {"slow_query_log_file", FILE_PATH, false},
  {"log_error", FILE_PATH, false},

  /* Built-in Aria and InnoDB storage locations. */
  {"aria_log_dir_path", DIRECTORY_PATH, true},
  {"innodb_data_home_dir", DIRECTORY_PATH, true},
  {"innodb_log_group_home_dir", DIRECTORY_PATH, true},
  {"innodb_undo_directory", DIRECTORY_PATH, true},
  {"innodb_buffer_pool_filename", FILE_PATH, false},
  {"innodb_data_file_path", INNODB_FILE_LIST, false},
  {"innodb_temp_data_file_path", INNODB_FILE_LIST, false},

  /* Optional components and storage engines; absent variables are skipped. */
  {"wsrep_data_home_dir", DIRECTORY_PATH, false},
  {"wsrep_sst_tmp_dir", DIRECTORY_PATH, false},
  {"wsrep_status_file", FILE_PATH, false},
  {"rocksdb_datadir", DIRECTORY_PATH, false},
  {"rocksdb_wal_dir", DIRECTORY_PATH, false},
  {"rocksdb_log_dir", DIRECTORY_PATH, false},
  {"rocksdb_persistent_cache_path", DIRECTORY_PATH, false},
  {"duckdb_temp_directory", DIRECTORY_PATH, false},
  {"server_audit_file_path", FILE_PATH, false},
  {"sql_error_log_filename", FILE_PATH, false},
  {"aws_key_management_keyfile_dir", DIRECTORY_PATH, false}
};

static ST_FIELD_INFO disk_size_info_fields[]=
{
  Show::Column("DIR_NAME", Show::Varchar(FN_REFLEN), NOT_NULL),
  Show::Column("RELATED_VARIABLES", Show::Varchar(1024), NOT_NULL),
  Show::Column("FREE_SIZE", Show::ULonglong(), NOT_NULL),
  Show::Column("TOTAL_SIZE", Show::ULonglong(), NOT_NULL),
  Show::CEnd()
};

static std::string directory_name(const std::string &filename)
{
  const std::string::size_type separator= filename.find_last_of("/\\");
  if (separator == std::string::npos)
    return ".";
  if (separator == 0)
    return filename.substr(0, 1);
  return filename.substr(0, separator);
}

static void split_value(const std::string &value, char separator,
                        std::vector<std::string> *parts)
{
  std::string::size_type begin= 0;
  do
  {
    const std::string::size_type end= value.find(separator, begin);
    const std::string part= value.substr(begin, end - begin);
    if (!part.empty())
      parts->push_back(part);
    if (end == std::string::npos)
      break;
    begin= end + 1;
  } while (begin <= value.length());
}

static void split_paths(const std::string &value,
                        std::vector<std::string> *paths)
{
#ifdef _WIN32
  split_value(value, ';', paths);
#else
  split_value(value, ':', paths);
#endif
}

static void parse_innodb_file_list(const std::string &value,
                                   std::vector<std::string> *paths)
{
  std::vector<std::string> specifications;
  split_value(value, ';', &specifications);
  for (const std::string &specification : specifications)
  {
    std::string::size_type separator= specification.find(':');
#ifdef _WIN32
    if (separator == 1 && specification.length() > 2)
      separator= specification.find(':', 2);
#endif
    const std::string filename= specification.substr(0, separator);
    if (!filename.empty())
      paths->push_back(directory_name(filename));
  }
}

static bool read_variable(THD *thd, const char *name, std::string *value)
{
  /*
    In MariaDB, find_sys_var()'s throw_error argument is historically
    inverted: true suppresses ER_UNKNOWN_SYSTEM_VARIABLE for an optional
    variable.
  */
  sys_var *variable= find_sys_var(thd, name, strlen(name), true);
  if (!variable)
    return false;

  String buffer;
  String *result= variable->val_str(&buffer, thd, OPT_GLOBAL,
                                    &null_clex_str);
  if (!result)
    return false;

  value->assign(result->ptr(), result->length());
  return true;
}

#ifndef _WIN32
static bool resolve_directory(const std::string &path,
                              const std::string &datadir,
                              std::string *resolved)
{
  if (path.empty())
    return false;

  std::string candidate= path;
  if (!test_if_hard_path(candidate.c_str()))
  {
    candidate= datadir;
    if (!candidate.empty() && candidate.back() != FN_LIBCHAR)
      candidate.push_back(FN_LIBCHAR);
    candidate.append(path);
  }

  char real_path[FN_REFLEN];
  if (candidate.length() >= sizeof(real_path) ||
      my_realpath(real_path, candidate.c_str(), MYF(0)))
    return false;
  resolved->assign(real_path);
  return true;
}

static std::string variables_as_json(const std::vector<std::string> &variables)
{
  std::string json("[");
  for (std::vector<std::string>::size_type i= 0; i < variables.size(); ++i)
  {
    if (i)
      json.push_back(',');
    json.push_back('"');
    json.append(variables[i]);
    json.push_back('"');
  }
  json.push_back(']');
  return json;
}

static int store_directory(THD *thd, TABLE *table, const std::string &path,
                           const std::vector<std::string> &variables)
{
  struct statvfs status;
  if (statvfs(path.c_str(), &status) != 0 || status.f_blocks == 0)
    return 0;

  const ulonglong block_size=
      status.f_frsize ? status.f_frsize : status.f_bsize;
  const ulonglong total= block_size * status.f_blocks;
  const ulonglong free= block_size * status.f_bavail;
  const std::string related_variables= variables_as_json(variables);

  table->field[0]->store(path.data(), path.length(), system_charset_info);
  table->field[1]->store(related_variables.data(), related_variables.length(),
                         system_charset_info);
  table->field[2]->store(free, true);
  table->field[3]->store(total, true);
  return schema_table_store_record(thd, table);
}
#endif

static int disk_size_info_fill_table(THD *thd, TABLE_LIST *tables, Item *)
{
  if (check_global_access(thd, FILE_ACL, true))
    return 0;

#ifdef _WIN32
  /*
    statvfs() is not available on Windows. Keep the plugin loadable there;
    no rows are returned.
  */
  return 0;
#else
  TABLE *table= tables->table;
  std::string datadir;
  if (!read_variable(thd, "datadir", &datadir) || datadir.empty())
    return 0;

  /*
    innodb_data_file_path and innodb_temp_data_file_path entries that don't
    specify an absolute path are placed under innodb_data_home_dir (which
    itself defaults to datadir), not directly under datadir. See
    ha_innodb.cc's srv_data_home computation and SysTablespace::set_path().
  */
  std::string innodb_data_home= datadir;
  std::string innodb_data_home_raw;
  if (read_variable(thd, "innodb_data_home_dir", &innodb_data_home_raw) &&
      !innodb_data_home_raw.empty())
  {
    std::string resolved;
    if (resolve_directory(innodb_data_home_raw, datadir, &resolved))
      innodb_data_home= resolved;
  }

  std::map<std::string, std::vector<std::string>> directories;
  for (const Path_variable &variable : path_variables)
  {
    std::string value;
    if (!read_variable(thd, variable.name, &value))
      continue;

    if (value.empty())
    {
      if (!variable.empty_means_datadir)
        continue;
      value= datadir;
    }

    std::vector<std::string> paths;
    if (variable.kind == PATH_LIST)
      split_paths(value, &paths);
    else if (variable.kind == INNODB_FILE_LIST)
      parse_innodb_file_list(value, &paths);
    else
    {
      if (variable.kind == FILE_PATH)
        value= directory_name(value);
      paths.push_back(value);
    }

    const std::string &base=
        variable.kind == INNODB_FILE_LIST ? innodb_data_home : datadir;
    for (const std::string &path : paths)
    {
      std::string resolved;
      if (!resolve_directory(path, base, &resolved))
        continue;

      std::vector<std::string> &variables= directories[resolved];
      bool already_present= false;
      for (const std::string &name : variables)
      {
        if (name == variable.name)
        {
          already_present= true;
          break;
        }
      }
      if (!already_present)
        variables.push_back(variable.name);
    }
  }

  for (const auto &directory : directories)
  {
    const int error=
        store_directory(thd, table, directory.first, directory.second);
    if (error)
      return error;
  }
  return 0;
#endif
}

static int disk_size_info_init(void *data)
{
  ST_SCHEMA_TABLE *schema= static_cast<ST_SCHEMA_TABLE *>(data);
  schema->fields_info= disk_size_info_fields;
  schema->fill_table= disk_size_info_fill_table;
  return 0;
}

static struct st_mysql_information_schema disk_size_info_descriptor=
{
  MYSQL_INFORMATION_SCHEMA_INTERFACE_VERSION
};

} // namespace

maria_declare_plugin(disk_size_info)
{
  MYSQL_INFORMATION_SCHEMA_PLUGIN,
  &disk_size_info_descriptor,
  "DISK_SIZE_INFO",
  "lefred (Frédéric Descamps)",
  "Disk capacity for MariaDB server directories",
  PLUGIN_LICENSE_GPL,
  disk_size_info_init,
  nullptr,
  0x0100,
  nullptr,
  nullptr,
  "1.0",
  MariaDB_PLUGIN_MATURITY_EXPERIMENTAL
}
maria_declare_plugin_end;
