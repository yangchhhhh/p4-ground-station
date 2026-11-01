#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class HumidApp : public systems::phone::App {
public:
    static HumidApp *requestInstance(bool use_status_bar = true, bool use_navigation_bar = true);
    ~HumidApp();

protected:
    HumidApp(bool use_status_bar, bool use_navigation_bar);
    bool run(void) override;
    bool back(void) override;

private:
    static HumidApp *_instance;
};

} // namespace esp_brookesia::apps
