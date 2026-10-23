/* SPDX-License-Identifier: MIT
 * bootl.rc reader — see bootl_rc.h for the format.
 */
#include "bootl_rc.h"

#include <Arduino.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

BootRC g_bootrc;

// PS/2 lines are driven by FabGL's ULP: they must be RTC GPIOs that can
// also output (34..39 are input-only).
static bool valid_ps2_gpio(int g) {
  switch (g) {
    // 2/12/13/14 are the SD card lines; 25 is the audio DAC.
    case 0: case 4: case 15: case 26: case 27: case 32: case 33:
      return true;
    default:
      return false;
  }
}

static bool valid_input_gpio(int g) {
  if (g < 0 || g > 39) return false;
  if (g >= 6 && g <= 11) return false;   // SPI flash
  if (g == 20 || g == 24 || (g >= 28 && g <= 31)) return false;
  return true;
}

static char *trim(char *s) {
  while (*s && isspace((unsigned char)*s)) ++s;
  char *e = s + strlen(s);
  while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
  return s;
}

static bool find_rc(const char *root, char *path, size_t size) {
  DIR *dir = opendir(root);
  if (!dir) return false;
  bool found = false;
  struct dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcasecmp(entry->d_name, "bootl.rc") == 0) {
      snprintf(path, size, "%s/%s", root, entry->d_name);
      found = true;
      break;
    }
  }
  closedir(dir);
  return found;
}

void bootrc_load(const char *sdMountPath) {
  g_bootrc = BootRC();
  char path[300];
  if (!sdMountPath || !find_rc(sdMountPath, path, sizeof(path))) {
    Serial.println("[MC1000][BOOTRC] bootl.rc nao encontrado; pinos padrao");
    return;
  }
  FILE *f = fopen(path, "r");
  if (!f) {
    Serial.printf("[MC1000][BOOTRC] erro ao abrir %s\n", path);
    return;
  }
  BootRC rc;
  char line[96];
  while (fgets(line, sizeof(line), f)) {
    char *s = trim(line);
    if (!*s || *s == '#' || *s == ';' || (s[0] == '/' && s[1] == '/')) continue;
    // Split "key=value" / "key: value" / "key value".
    char *sep = s;
    while (*sep && *sep != '=' && *sep != ':' && !isspace((unsigned char)*sep)) ++sep;
    if (!*sep) continue;
    *sep++ = 0;
    char *key = trim(s);
    char *val = trim(sep);
    while (*val == '=' || *val == ':') val = trim(val + 1);
    char *end = nullptr;
    const long v = strtol(val, &end, 10);
    if (end == val) {
      Serial.printf("[MC1000][BOOTRC] valor invalido: %s=%s\n", key, val);
      continue;
    }
    if (strcasecmp(key, "kbddat") == 0) rc.kbddat = (int)v;
    else if (strcasecmp(key, "kbdclk") == 0) rc.kbdclk = (int)v;
    else if (strcasecmp(key, "magicb") == 0) rc.magicb = (int)v;
    else Serial.printf("[MC1000][BOOTRC] chave ignorada: %s\n", key);
  }
  fclose(f);

  if (!valid_ps2_gpio(rc.kbddat) || !valid_ps2_gpio(rc.kbdclk) ||
      rc.kbddat == rc.kbdclk) {
    Serial.printf("[MC1000][BOOTRC] pinos PS/2 invalidos (dat=%d clk=%d); usando 32/33\n",
                  rc.kbddat, rc.kbdclk);
    rc.kbddat = 32;
    rc.kbdclk = 33;
  }
  if (rc.magicb >= 0 &&
      (!valid_input_gpio(rc.magicb) || rc.magicb == rc.kbddat || rc.magicb == rc.kbdclk)) {
    Serial.printf("[MC1000][BOOTRC] magicb=%d invalido; desativado\n", rc.magicb);
    rc.magicb = -1;
  }
  rc.loaded = true;
  g_bootrc = rc;
  Serial.printf("[MC1000][BOOTRC] %s: kbddat=%d kbdclk=%d magicb=%d\n",
                path, rc.kbddat, rc.kbdclk, rc.magicb);
}
