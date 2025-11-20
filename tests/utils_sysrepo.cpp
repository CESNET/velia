#include "trompeloeil_doctest.h"
#include "system_vars.h"
#include "tests/pretty_printers.h"
#include "tests/test_log_setup.h"
#include "utils/sysrepo.h"

using namespace std::literals;

TEST_CASE("sysrepo utils")
{
    TEST_INIT_LOGS;
    TEST_SYSREPO_INIT;
    srSess.sendRPC(srSess.getContext().newPath("/ietf-factory-default:factory-reset"));

    SECTION("Values to yang return edit's first sibling")
    {
        srSess.switchDatastore(sysrepo::Datastore::Operational);

        srSess.setItem("/ietf-system:system-state/platform/os-name", "GNU/Linux");
        srSess.applyChanges();

        auto edit = srSess.operationalChanges();
        REQUIRE(edit);
        REQUIRE(*edit->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::Siblings) == R"({
  "ietf-system:system-state": {
    "@": {
      "ietf-origin:origin": "ietf-origin:unknown"
    },
    "platform": {
      "os-name": "GNU/Linux"
    }
  }
}
)"s);

        velia::utils::valuesToYang({{"/ietf-interfaces:interfaces/interface[name='eth0']/name", "eth0"}}, {}, {}, srSess, edit);

        REQUIRE(edit);
        REQUIRE(*edit->printStr(libyang::DataFormat::JSON, libyang::PrintFlags::Siblings) == R"({
  "ietf-interfaces:interfaces": {
    "interface": [
      {
        "name": "eth0"
      }
    ]
  },
  "ietf-system:system-state": {
    "@": {
      "ietf-origin:origin": "ietf-origin:unknown"
    },
    "platform": {
      "os-name": "GNU/Linux"
    }
  }
}
)"s);
    }
}
