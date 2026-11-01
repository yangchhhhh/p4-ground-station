#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class Game2048App : public systems::phone::App {
public:
    static Game2048App *requestInstance(bool use_status_bar = true, bool use_navigation_bar = true);
    ~Game2048App();

protected:
    Game2048App(bool use_status_bar, bool use_navigation_bar);
    bool run(void) override;
    bool back(void) override;

private:
    static Game2048App *_instance;
};

} // namespace esp_brookesia::apps
