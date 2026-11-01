#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class GpsApp : public systems::phone::App {
public:
    static GpsApp *requestInstance(bool use_status_bar = true, bool use_navigation_bar = true);
    ~GpsApp();

protected:
    GpsApp(bool use_status_bar, bool use_navigation_bar);
    bool run(void) override;
    bool back(void) override;

private:
    static GpsApp *_instance;
};

} // namespace esp_brookesia::apps
