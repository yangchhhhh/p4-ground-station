#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class MapApp : public systems::phone::App {
public:
    static MapApp *requestInstance(bool use_status_bar = true, bool use_navigation_bar = true);
    ~MapApp();

protected:
    MapApp(bool use_status_bar, bool use_navigation_bar);
    bool run(void) override;
    bool back(void) override;

private:
    static MapApp *_instance;
};

} // namespace esp_brookesia::apps
