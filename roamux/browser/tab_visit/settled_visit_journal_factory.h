// SPDX-License-Identifier: Apache-2.0
#ifndef ROAMUX_BROWSER_TAB_VISIT_SETTLED_VISIT_JOURNAL_FACTORY_H_
#define ROAMUX_BROWSER_TAB_VISIT_SETTLED_VISIT_JOURNAL_FACTORY_H_

#include "base/no_destructor.h"
#include "chrome/browser/profiles/profile_keyed_service_factory.h"

class Profile;

namespace roamux::tab_visit {

class SettledVisitJournalService;

// Profile policy (roam-21 / I-4.1): regular profiles get their OWN journal on
// disk and OTR gets a SEPARATE in-memory journal — never redirected to the
// original profile, so incognito visits never touch disk nor leak into the
// regular journal. (This file is compiled into the upstream tab target via
// patch 0015's `sources +=`; it needs //chrome/browser/profiles.)
class SettledVisitJournalFactory : public ProfileKeyedServiceFactory {
 public:
  static SettledVisitJournalService* GetForProfile(Profile* profile);
  static SettledVisitJournalFactory* GetInstance();

 private:
  friend class base::NoDestructor<SettledVisitJournalFactory>;
  SettledVisitJournalFactory();
  ~SettledVisitJournalFactory() override;

  // BrowserContextKeyedServiceFactory:
  std::unique_ptr<KeyedService> BuildServiceInstanceForBrowserContext(
      content::BrowserContext* context) const override;
};

}  // namespace roamux::tab_visit

#endif  // ROAMUX_BROWSER_TAB_VISIT_SETTLED_VISIT_JOURNAL_FACTORY_H_
