#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class StatusApp : public systems::phone::App {
public:
    static StatusApp *requestInstance(bool use_status_bar = true, bool use_navigation_bar = true);
    ~StatusApp();

protected:
    StatusApp(bool use_status_bar, bool use_navigation_bar);
    bool run(void) override;
    bool back(void) override;

private:
    static StatusApp *_instance;
};

} // namespace esp_brookesia::apps
