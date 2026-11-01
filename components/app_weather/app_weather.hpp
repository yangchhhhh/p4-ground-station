#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class WeatherApp : public systems::phone::App {
public:
    static WeatherApp *requestInstance(bool use_status_bar = true, bool use_navigation_bar = true);
    ~WeatherApp();

protected:
    WeatherApp(bool use_status_bar, bool use_navigation_bar);
    bool run(void) override;
    bool back(void) override;

private:
    static WeatherApp *_instance;
};

} // namespace esp_brookesia::apps
