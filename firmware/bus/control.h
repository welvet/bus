#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

constexpr uint32_t MOTOR_LEASE_MS = 1500;
struct Control {
  int speed = 0;
  int angle = -1;
  uint32_t lastCommand = 0;
  void motor(int value, uint32_t now) { speed = value; lastCommand = now; }
  bool expire(uint32_t now) {
    if (speed && uint32_t(now - lastCommand) >= MOTOR_LEASE_MS) {
      speed = 0;
      return true;
    }
    return false;
  }
};
inline bool integer(const char *text, int minimum, int maximum, int &result) {
  while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;
  const char *digits = *text == '-' ? text + 1 : text;
  if (*digits < '0' || *digits > '9') return false;
  if (*digits == '0' && digits[1] >= '0' && digits[1] <= '9') return false;
  char *end;
  long value = strtol(text, &end, 10);
  while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
  if (*end || value < minimum || value > maximum) return false;
  result = int(value);
  return true;
}
inline bool headerName(const char *line, const char *name) {
  while (*name) {
    char c = *line++;
    if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    if (c != *name++) return false;
  }
  return true;
}
struct Request {
  char method[8] = {}, path[64] = {};
  const char *body = nullptr;
  bool json = false, crossSite = false;
};
// Input must be NUL-terminated; no chunked requests or oversized bodies.
// 0=incomplete, 1=ready, -1=invalid.
inline int parseRequest(const char *data, size_t size, Request &request) {
  const char *headersEnd = strstr(data, "\r\n\r\n");
  if (!headersEnd) return 0;
  const char *firstEnd = strstr(data, "\r\n");
  if (!firstEnd || firstEnd - data >= 128) return -1;
  char first[128], version[16], extra;
  memcpy(first, data, firstEnd - data); first[firstEnd - data] = 0;
  if (sscanf(first, "%7s %63s %15s %c", request.method, request.path, version, &extra) != 3 ||
      (strcmp(version, "HTTP/1.1") && strcmp(version, "HTTP/1.0"))) return -1;
  int length = 0;
  bool hasLength = false;
  for (const char *line = firstEnd + 2; line < headersEnd; ) {
    const char *end = strstr(line, "\r\n");
    if (!end || end - line >= 512) return -1;
    char header[512]; memcpy(header, line, end - line); header[end - line] = 0;
    const char *colon = strchr(header, ':');
    if (!colon) return -1;
    const char *value = colon + 1;
    while (*value == ' ' || *value == '\t') ++value;
    if (headerName(header, "content-length:")) {
      if (hasLength || !integer(value, 0, 16, length)) return -1;
      hasLength = true;
    }
    if (headerName(header, "transfer-encoding:")) return -1;
    if (headerName(header, "content-type:")) request.json = !strcmp(value, "application/json");
    if (headerName(header, "sec-fetch-site:")) request.crossSite = !strcmp(value, "cross-site");
    line = end + 2;
  }
  request.body = headersEnd + 4;
  size_t needed = size_t(request.body - data) + length;
  if (size < needed) return 0;
  if (size != needed) return -1;
  if (!strcmp(request.method, "POST") && (!hasLength || length == 0)) return -1;
  return 1;
}
