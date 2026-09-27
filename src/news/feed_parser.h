// RSS 2.0 / Atom feed parsing into articles (title, link, date, summary).
// A deliberately small, tolerant tag scanner: news feeds are simple and
// often not well-formed enough for a strict XML parser.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "src/store/db.h"

namespace twn::news {

std::vector<store::Article> ParseFeed(std::string_view xml, const std::string& source);

// Parses one article per line: {"title", "url", "source", "published", "summary"}.
std::vector<store::Article> ParseJsonLines(std::string_view text, std::string* error);

// Decodes entities/CDATA and strips HTML tags; collapses whitespace.
std::string CleanText(std::string_view s);

}  // namespace twn::news
