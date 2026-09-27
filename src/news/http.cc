#include "src/news/http.h"

#include <mutex>

#include "curl/curl.h"

namespace twn::news {
namespace {

size_t Append(char* data, size_t size, size_t n, void* user) {
  static_cast<std::string*>(user)->append(data, size * n);
  return size * n;
}

void GlobalInit() {
  static std::once_flag once;
  std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

HttpResponse Perform(const std::string& url, const std::string* post_body,
                     const std::vector<std::string>& headers, long timeout_s) {
  GlobalInit();
  HttpResponse r;
  CURL* curl = curl_easy_init();
  if (!curl) {
    r.error = "curl_easy_init failed";
    return r;
  }
  curl_slist* list = nullptr;
  for (const std::string& h : headers) list = curl_slist_append(list, h.c_str());
  if (post_body) list = curl_slist_append(list, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_s);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "twn_election/0.2 (+news monitor)");
  curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // any supported compression
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, Append);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &r.body);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  if (list) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
  if (post_body) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_body->c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(post_body->size()));
  }
  const CURLcode rc = curl_easy_perform(curl);
  if (rc != CURLE_OK) {
    r.error = curl_easy_strerror(rc);
  } else {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &r.status);
  }
  curl_slist_free_all(list);
  curl_easy_cleanup(curl);
  return r;
}

}  // namespace

HttpResponse HttpGet(const std::string& url, const std::vector<std::string>& headers,
                     long timeout_s) {
  return Perform(url, nullptr, headers, timeout_s);
}

HttpResponse HttpPostJson(const std::string& url, const std::string& json_body,
                          const std::vector<std::string>& headers, long timeout_s) {
  return Perform(url, &json_body, headers, timeout_s);
}

}  // namespace twn::news
