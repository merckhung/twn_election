// Candidate portraits: loads assets/photos/<candidate id>.{jpg,png} when
// present, otherwise renders a party-coloured placeholder with the
// candidate's surname so every candidate has a recognisable avatar.
#pragma once

#include <string>
#include <unordered_map>

#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "src/election/model.h"
#include "src/ui/text.h"

namespace twn::ui {

class Avatars {
 public:
  Avatars(const Fonts* fonts, std::string data_root) : fonts_(fonts), root_(std::move(data_root)) {}

  // Draws the candidate's avatar as a circle of diameter `size` at (x, y) (top-left).
  void Draw(SkCanvas* c, const election::Candidate& cand, const election::Party& party, float x,
            float y, float size, bool ring = true);

  bool HasPhoto(const election::Candidate& cand);
  int photo_count() const;

 private:
  sk_sp<SkImage> Photo(const election::Candidate& cand);

  const Fonts* fonts_;
  std::string root_;
  std::unordered_map<std::string, sk_sp<SkImage>> photos_;  // null = no photo
};

}  // namespace twn::ui
