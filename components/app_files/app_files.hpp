#pragma once

#include "systems/phone/esp_brookesia_phone_app.hpp"

namespace esp_brookesia::apps {

class FilesApp : public systems::phone::App {
public:
    static FilesApp *requestInstance(bool use_status_bar = true, bool use_navigation_bar = true);
    ~FilesApp();

protected:
    FilesApp(bool use_status_bar, bool use_navigation_bar);
    bool run(void) override;
    bool back(void) override;

private:
    static FilesApp *_instance;
};

} // namespace esp_brookesia::apps
