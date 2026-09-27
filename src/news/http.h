// Minimal HTTP client (libcurl). Also reads file:// URLs, handy for tests and
// for dropping feeds on disk.
#pragma once

#include <string>
#include <vector>

namespace twn::news {

struct HttpResponse {
  long status = 0;
  std::string body;
  std::string error;  // transport error, empty on success
  bool ok() const { return error.empty() && (status == 0 || (status >= 200 && status < 300)); }
};

HttpResponse HttpGet(const std::string& url, const std::vector<std::string>& headers = {},
                     long timeout_s = 20);
HttpResponse HttpPostJson(const std::string& url, const std::string& json_body,
                          const std::vector<std::string>& headers = {}, long timeout_s = 60);

}  // namespace twn::news
