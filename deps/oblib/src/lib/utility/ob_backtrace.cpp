/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX LIB

#include "lib/utility/ob_backtrace.h"
#include <stdio.h>
#include <execinfo.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include "lib/utility/ob_defer.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/coro/co_var.h"

namespace oceanbase
{
namespace common
{

int ob_backtrace(void **buffer, int size)
{
  int rv = 0;
  if (OB_LIKELY(g_enable_backtrace)) {
    rv = backtrace(buffer, size);
  }
  return rv;
}

bool read_codesegment_addr_range(int64_t &start_addr, int64_t &end_addr)
{
  bool bret = false;
  char fn[64];
  snprintf(fn, sizeof(fn), "/proc/%d/maps", getpid());
  FILE *map_file = fopen(fn, "rt");
  if (!map_file) return bret;
  DEFER(fclose(map_file));
  char buff[1024];
  while (fgets(buff, sizeof buff, map_file) != NULL) {
    int len = strlen(buff);
    if (len > 0 && buff[len-1] == '\n') {
      buff[--len] = '\0';
    }
    char* tokens[8];
    int n_token = 0;
    for (char *saveptr = NULL, *value = strtok_r(buff, " ", &saveptr);
         NULL != value && n_token < ARRAYSIZEOF(tokens);
         value = strtok_r(NULL, " ", &saveptr)) {
      tokens[n_token++] = value;
    }
    if (n_token < 6) {
      continue;
    }
    char *perms = tokens[1];
    if (strlen(perms) == 4 && perms[2] != 'x') {
      continue;
    }
    int64_t a0 = 0, a1 = 0;
    sscanf(tokens[0], "%lx-%lx", &a0, &a1);
    char *path = tokens[5];
    if (path[0] == '[') {
      continue;
    }
    int64_t addr = (int64_t)__func__;
    if (addr >= a0 && addr < a1) {
      start_addr = a0;
      end_addr = a1;
      bret = true;
      break;
    }
  }
  return bret;
}

bool g_enable_backtrace = true;
bool g_use_rel_offset = false;
struct ProcMapInfo
{
  int64_t code_start_addr_;
  int64_t code_end_addr_;
  bool is_inited_;
};

ProcMapInfo g_proc_map_info{.code_start_addr_ = -1, .code_end_addr_ = -1, .is_inited_ = false};

void init_proc_map_info()
{
  read_codesegment_addr_range(g_proc_map_info.code_start_addr_, g_proc_map_info.code_end_addr_);
  g_proc_map_info.is_inited_ = true;
}

int64_t get_rel_offset(int64_t addr)
{
  if (g_use_rel_offset) {
    int64_t code_start_addr = -1;
    int64_t code_end_addr = -1;
    if (OB_UNLIKELY(!g_proc_map_info.is_inited_)) {
      read_codesegment_addr_range(code_start_addr, code_end_addr);
    } else {
      code_start_addr = g_proc_map_info.code_start_addr_;
      code_end_addr = g_proc_map_info.code_end_addr_;
    }
    if (code_start_addr != -1) {
      if (OB_LIKELY(addr >= code_start_addr && addr < code_end_addr)) {
        addr -= code_start_addr;
      }
    }
  }
  return addr;
}


constexpr int MAX_ADDRS_COUNT = 100;
using PointerBuf = void *[MAX_ADDRS_COUNT];
RLOCAL(PointerBuf, addrs);
RLOCAL(ByteBuf<LBT_BUFFER_LENGTH>, buffer);

char *lbt()
{
  int size = OB_BACKTRACE_M(addrs, MAX_ADDRS_COUNT);
  return parray(*&buffer, LBT_BUFFER_LENGTH, (int64_t *)addrs, size);
}

char *lbt(char *buf, int32_t len)
{
  int size = OB_BACKTRACE_M(addrs, MAX_ADDRS_COUNT);
  return parray(buf, len, (int64_t *)addrs, size);
}

char *parray(int64_t *array, int size)
{
  return parray(buffer, LBT_BUFFER_LENGTH, array, size);
}

char *parray(char *buf, int64_t len, int64_t *array, int size)
{
  //As used in lbt, and lbt used when print error log.
  //Can not print error log this function.
  if (NULL != buf && len > 0 && NULL != array) {
    int64_t pos = 0;
    int64_t count = 0;
    for (int64_t i = 0; i < size; i++) {
      int64_t addr = get_rel_offset(array[i]);
      if (0 == i) {
        count = snprintf(buf + pos, len - pos, "0x%lx", addr);
      } else {
        count = snprintf(buf + pos, len - pos, " 0x%lx", addr);
      }
      if (count >= 0 && pos + count < len) {
        pos += count;
      } else {
        // buf not enough
        break;
      }
    }
    buf[pos] = 0;
  }
  return buf;
}

EXTERN_C_BEGIN
int ob_backtrace_c(void **buffer, int size)
{
  return OB_BACKTRACE_M(buffer, size);
}
char *parray_c(char *buf, int64_t len, int64_t *array, int size)
{
  return parray(buf, len, array, size);
}
int64_t get_rel_offset_c(int64_t addr)
{
  return get_rel_offset(addr);
}
EXTERN_C_END

} // end namespace common
} // end namespace oceanbase
