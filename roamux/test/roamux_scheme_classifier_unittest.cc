// SPDX-License-Identifier: Apache-2.0
#include "chrome/browser/autocomplete/chrome_autocomplete_scheme_classifier.h"

#include "chrome/test/base/testing_profile.h"
#include "content/public/test/browser_task_environment.h"
#include "roamux/common/roamux_url_constants.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/metrics_proto/omnibox_input_type.pb.h"

namespace roamux {
namespace {

// roam-91 (D8): typing roamux://about in the omnibox must classify as URL
// input, not a search. ChromeAutocompleteSchemeClassifier consults
// ProfileIOData::IsHandledProtocol, where patch 0028 lists "roamux" — this
// pins that the typed-input path reaches the alias rewrite at all.
TEST(RoamuxSchemeClassifierTest, RoamuxSchemeClassifiesAsUrlInput) {
  content::BrowserTaskEnvironment task_environment;
  TestingProfile profile;
  ChromeAutocompleteSchemeClassifier classifier(&profile);
  EXPECT_EQ(metrics::OmniboxInputType::URL,
            classifier.GetInputTypeForScheme(kRoamuxScheme));
}

} // namespace
} // namespace roamux
