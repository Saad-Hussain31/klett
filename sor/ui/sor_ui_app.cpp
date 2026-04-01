#include "ui/sor_controller.h"
#include "ui/sor_ui.h"
#include "infra/logging.h"

#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    std::string config_path;
    if (argc > 1)
        config_path = argv[1];

    sor::ui::SorController controller;

    if (!controller.initialize(config_path))
    {
        std::cerr << "Failed to initialize SOR backend\n";
        return 1;
    }

    sor::ui::SorUI ui(controller);
    if (!ui.initialize())
    {
        std::cerr << "Failed to initialize UI\n";
        return 1;
    }

    controller.start();
    ui.run(); // blocks until window close
    controller.stop();
    ui.shutdown();

    return 0;
}
