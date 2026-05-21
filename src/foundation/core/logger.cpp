#include "foundation/core/logger.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace {
std::mutex g_logMutex;
Logger::Level g_level = Logger::Level::Debug;
} // namespace

void Logger::setLevel(Level lvl) { g_level = lvl; }

Logger::Level Logger::getLevel() { return g_level; }

static const char *levelToStr(Logger::Level l) {
  switch (l) {
  case Logger::Level::Debug:
    return "DBG";
  case Logger::Level::Info:
    return "INF";
  case Logger::Level::Warn:
    return "WRN";
  case Logger::Level::Error:
    return "ERR";
  case Logger::Level::Fatal:
    return "FTL";
  }
  return "UNK";
}

void Logger::vlog(Level lvl, const char *file, int line, const char *fmt, va_list args) {
  if (lvl < g_level)
    return;

  std::lock_guard<std::mutex> lk(g_logMutex);

  // timestamp
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  char timestr[32];
  std::strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", std::localtime(&t));

  // format message
  char msgbuf[1024];
  vsnprintf(msgbuf, sizeof(msgbuf), fmt, args);

  std::ostringstream out;
  out << timestr << ' ' << levelToStr(lvl) << ' ' << file << ':' << line << " " << msgbuf;
  std::string outstr = out.str();

  std::clog << outstr << std::endl;

  if (lvl == Level::Fatal) {
    std::abort();
  }
}

void Logger::log(Level lvl, const char *file, int line, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  Logger::vlog(lvl, file, line, fmt, ap);
  va_end(ap);
}
