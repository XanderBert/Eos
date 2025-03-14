﻿#include "EOS.h"

int main()
{
    //TODO Wrap window and get display based on distro
    uint32_t width{}, height{};
    GLFWwindow* window = EOS::InitWindow("Hello, World!", width, height);
    if (!window){ printf("Failed to Create a window.\n"); }

    EOS::ContextCreationDescription contextDescr
    {
        .config =
     {
            .enableValidationLayers = true
        },
        .window                 = window,
        .display                = nullptr,
        .preferredHardwareType  = EOS::HardwareDeviceType::Discrete
    };

    std::unique_ptr<EOS::IContext> context = EOS::CreateContextWithSwapchain(contextDescr);


    //while (!glfwWindowShouldClose(window))
    //{
    //    glfwPollEvents();
    //}

    EOS::DestroyWindow(window);
    return 0;
}