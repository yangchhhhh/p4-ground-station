#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class DroneApp : public systems::phone::App {
public:
    static DroneApp *requestInstance(bool use_status_bar = true, bool use_navigation_bar = true);
    ~DroneApp();

protected:
    DroneApp(bool use_status_bar, bool use_navigation_bar);
    bool run(void) override;
    bool back(void) override;

private:
    static DroneApp *_instance;
};

} // namespace esp_brookesia::apps
