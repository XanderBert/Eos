#include "vulkanClasses.h"

#include <complex>
#include <cstring>
#include <ranges>

#include "vulkan/vkTools.h"

#pragma region GLOBAL_FUNCTIONS
void cmdPipelineBarrier(const EOS::ICommandBuffer& commandBuffer, const std::vector<EOS::GlobalBarrier>& globalBarriers, const std::vector<EOS::ImageBarrier>& imageBarriers)
{
    const CommandBuffer* cmdBuffer = static_cast<const CommandBuffer*>(&commandBuffer);
    CHECK(cmdBuffer, "The commandBuffer is not valid");

    std::vector<VkMemoryBarrier2KHR> vkMemoryBarriers;
    vkMemoryBarriers.reserve(globalBarriers.size());

    std::vector<VkImageMemoryBarrier2KHR> vkImageBarriers;
    vkImageBarriers.reserve(imageBarriers.size());

    for (const auto& barrier : globalBarriers)
    {
        VkMemoryBarrier2 vkBarrier
        {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR,
            .pNext = nullptr,
            .srcStageMask   = VkSynchronization::ConvertToVkPipelineStage2(barrier.CurrentState),
            .srcAccessMask  = VkSynchronization::ConvertToVkAccessFlags2(barrier.CurrentState),
            .dstStageMask   = VkSynchronization::ConvertToVkPipelineStage2(barrier.NextState),
            .dstAccessMask  = VkSynchronization::ConvertToVkAccessFlags2(barrier.NextState)
        };

        vkMemoryBarriers.emplace_back(vkBarrier);
    }
    VulkanTexturePool&  texturePool = cmdBuffer->VkContext->TexturePool;

    for (const auto&[Texture, CurrentState, NextState] : imageBarriers)
    {
        const VulkanImage& currentImage = *texturePool.Get(Texture);

        VkImageAspectFlags aspectMask = VkSynchronization::ConvertToVkImageAspectFlags(CurrentState);
        if (VulkanImage::IsDepthAttachment(currentImage))
        {
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }

        VkImageMemoryBarrier2 vkBarrier
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,
            .pNext = nullptr,
            .srcStageMask       = VkSynchronization::ConvertToVkPipelineStage2(CurrentState),
            .srcAccessMask      = VkSynchronization::ConvertToVkAccessFlags2(CurrentState),
            .dstStageMask       = VkSynchronization::ConvertToVkPipelineStage2(NextState),
            .dstAccessMask      = VkSynchronization::ConvertToVkAccessFlags2(NextState),
            .oldLayout          = VkSynchronization::ConvertToVkImageLayout(CurrentState),
            .newLayout          = VkSynchronization::ConvertToVkImageLayout(NextState),
            .image              = currentImage.Image,
            .subresourceRange   = {aspectMask, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS}
        };

        vkImageBarriers.emplace_back(vkBarrier);
    }

    const VkDependencyInfoKHR dependencyInfo
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = static_cast<uint32_t>(vkMemoryBarriers.size()),
        .pMemoryBarriers = vkMemoryBarriers.data(),
        .bufferMemoryBarrierCount = 0,
        .pBufferMemoryBarriers = nullptr,
        .imageMemoryBarrierCount = static_cast<uint32_t>(vkImageBarriers.size()),
        .pImageMemoryBarriers = vkImageBarriers.data()
    };

    vkCmdPipelineBarrier2(cmdBuffer->CommandBufferImpl->VulkanCommandBuffer, &dependencyInfo);
}
#pragma endregion



VulkanImage::VulkanImage(const ImageDescription &description)
: Image(description.Image)
, UsageFlags(description.UsageFlags)
, Extent(description.Extent)
, ImageType(description.ImageType)
, ImageFormat(description.ImageFormat)
, Levels(description.Levels)
, Layers(description.Layers)
{
    VK_ASSERT(VkDebug::SetDebugObjectName(description.Device, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(Image), description.DebugName));
    CreateImageView(ImageView, description.Device, Image, ImageType, ImageFormat, Levels, Layers, description.DebugName);
}

VkImageType VulkanImage::ToImageType(const EOS::ImageType imageType)
{
    switch (imageType)
    {
        case (EOS::ImageType::Image_1D):
        case (EOS::ImageType::Image_1D_Array):
            return VK_IMAGE_TYPE_1D;

        case(EOS::ImageType::Image_2D):
        case(EOS::ImageType::Image_2D_Array):
        case(EOS::ImageType::CubeMap):
        case(EOS::ImageType::CubeMap_Array):
        case(EOS::ImageType::SwapChain):
            return VK_IMAGE_TYPE_2D;

        case(EOS::ImageType::Image_3D):
            return  VK_IMAGE_TYPE_3D;
    }

    return VK_IMAGE_TYPE_MAX_ENUM;
}

VkImageViewType VulkanImage::ToImageViewType(EOS::ImageType imageType)
{
    switch (imageType)
    {
        case (EOS::ImageType::Image_1D):
            return VK_IMAGE_VIEW_TYPE_1D;
        case (EOS::ImageType::Image_1D_Array):
            return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
        case(EOS::ImageType::Image_2D):
            return VK_IMAGE_VIEW_TYPE_2D;
        case(EOS::ImageType::Image_2D_Array):
            return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        case(EOS::ImageType::CubeMap):
            return VK_IMAGE_VIEW_TYPE_CUBE;
        case(EOS::ImageType::CubeMap_Array):
            return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
        case(EOS::ImageType::SwapChain):
            return VK_IMAGE_VIEW_TYPE_2D;
        case(EOS::ImageType::Image_3D):
            return  VK_IMAGE_VIEW_TYPE_3D;
    }

    return VK_IMAGE_VIEW_TYPE_MAX_ENUM;
}

void VulkanImage::CreateImageView(VkImageView& imageView, VkDevice device, VkImage image, const EOS::ImageType imageType,
    const VkFormat &imageFormat, const uint32_t levels, const uint32_t layers, const char *debugName)
{
    const VkImageViewCreateInfo createInfo =
   {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = ToImageViewType(imageType),
        .format = imageFormat,
        .components =  {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels , 0, layers},
    };

    VK_ASSERT(vkCreateImageView(device, &createInfo, nullptr, &imageView));
    VK_ASSERT(VkDebug::SetDebugObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(imageView), debugName));
}


VulkanSwapChain::VulkanSwapChain(const VulkanSwapChainCreationDescription& vulkanSwapChainDescription)
: VkContext(vulkanSwapChainDescription.vulkanContext)
, GraphicsQueue(vulkanSwapChainDescription.vulkanContext->VulkanDeviceQueues.Graphics.Queue)
{
    CHECK(VkContext, "VulkanContext is not valid.");
    CHECK(GraphicsQueue, "GraphicsQueue is not valid.");


    //Get details of what we support
    const VulkanSwapChainSupportDetails supportDetails{*VkContext};

    //Get the Surface Format (format and color space)
    SurfaceFormat = GetSwapChainFormat(supportDetails.formats, VkContext->Configuration.DesiredSwapChainColorSpace);
    VkPresentModeKHR presentMode {VK_PRESENT_MODE_FIFO_KHR};

    // Try using Immediate mode presenting if we are running on a linux machine
    // For Windows we try to use Mailbox mode
    // If they are not available we use FIFO
#if defined(EOS_PLATFORM_WAYLAND) || defined(EOS_PLATFORM_X11)
    if (std::ranges::find(supportDetails.presentModes, VK_PRESENT_MODE_IMMEDIATE_KHR) != supportDetails.presentModes.cend())
    {
        presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }
#elif defined(EOS_PLATFORM_WIN32)
    if (std::ranges::find(supportDetails.presentModes, VK_PRESENT_MODE_MAILBOX_KHR) != supportDetails.presentModes.cend())
    {
        presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
    }
#endif

    // Check The surface and QueueFamilyIndex
    VkBool32 queueFamilySupportsPresentation = VK_FALSE;
    VK_ASSERT(vkGetPhysicalDeviceSurfaceSupportKHR(VkContext->VulkanPhysicalDevice, VkContext->VulkanDeviceQueues.Graphics.QueueFamilyIndex, VkContext->VulkanSurface, &queueFamilySupportsPresentation));
    CHECK(queueFamilySupportsPresentation == VK_TRUE, "The queue family does not support presentation");

    //Get the device format properties
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(VkContext->VulkanPhysicalDevice, SurfaceFormat.format, &properties);

    // Get the imageUsageFlags
    VkImageUsageFlags usageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    const bool isStorageSupported = (supportDetails.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_STORAGE_BIT) > 0;
    const bool isTilingOptimalSupported = (properties.optimalTilingFeatures & VK_IMAGE_USAGE_STORAGE_BIT > 0);

    if (isStorageSupported && isTilingOptimalSupported) { usageFlags |= VK_IMAGE_USAGE_STORAGE_BIT; }

    const bool isCompositeAlphaSupported = (supportDetails.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0;

    //Create SwapChain
    const VkSwapchainCreateInfoKHR createInfo
    {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = VkContext->VulkanSurface,
        .minImageCount = supportDetails.capabilities.minImageCount,
        .imageFormat = SurfaceFormat.format,
        .imageColorSpace = SurfaceFormat.colorSpace,
        .imageExtent = {.width = vulkanSwapChainDescription.width, .height = vulkanSwapChainDescription.height},
        .imageArrayLayers = 1,
        .imageUsage = usageFlags,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 1,
        .pQueueFamilyIndices = &VkContext->VulkanDeviceQueues.Graphics.QueueFamilyIndex,
        .preTransform = supportDetails.capabilities.currentTransform,
        .compositeAlpha = isCompositeAlphaSupported ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
    };
    VK_ASSERT(vkCreateSwapchainKHR(VkContext->VulkanDevice, &createInfo, nullptr, &SwapChain);)

    //Create SwapChain Images
    VK_ASSERT(vkGetSwapchainImagesKHR(VkContext->VulkanDevice,SwapChain, &NumberOfSwapChainImages, nullptr);)
    NumberOfSwapChainImages = std::min(NumberOfSwapChainImages, MAX_IMAGES);  //Make sure we don't go over our max defined SwapChain images
    std::vector<VkImage> swapChainImages(NumberOfSwapChainImages);
    VK_ASSERT(vkGetSwapchainImagesKHR(VkContext->VulkanDevice, SwapChain, &NumberOfSwapChainImages,swapChainImages.data()));
    CHECK(NumberOfSwapChainImages >  0, "Number of SwapChain images is 0");
    CHECK(!swapChainImages.empty(), "The SwapChain images didn't got created");

    AcquireSemaphores.clear();
    AcquireSemaphores.reserve(NumberOfSwapChainImages);

    Textures.clear();
    Textures.reserve(NumberOfSwapChainImages);

    TimelineWaitValues.resize(NumberOfSwapChainImages);

    ImageDescription swapChainImageDescription
    {
        .Image = {},
        .UsageFlags = usageFlags,
        .Extent = VkExtent3D{.width = vulkanSwapChainDescription.width, .height = vulkanSwapChainDescription.height, .depth = 1},
        .ImageType = EOS::ImageType::SwapChain,
        .ImageFormat = SurfaceFormat.format,
        .Device = VkContext->VulkanDevice,
    };

    // create images, image views and framebuffers
    for (uint32_t i{}; i < NumberOfSwapChainImages; ++i)
    {
        //Create our Acquire Semaphore for this swapChain image
        AcquireSemaphores.emplace_back(VkSynchronization::CreateSemaphore(vulkanSwapChainDescription.vulkanContext->VulkanDevice, "SwapChain Acquire Semaphore: " + i));

        //Create a image
        swapChainImageDescription.Image = swapChainImages[i];
        swapChainImageDescription.DebugName = fmt::format("SwapChain Image: {}", i).c_str();
        VulkanImage swapChainImage{swapChainImageDescription};

        Textures.emplace_back(vulkanSwapChainDescription.vulkanContext->TexturePool.Create(std::move(swapChainImage)));
    }
}

VulkanSwapChain::~VulkanSwapChain()
{
    CHECK(VkContext, "VkContext is no longer valid");
    CHECK(SwapChain != VK_NULL_HANDLE, "The VkSwapChain is no longer valid");
    CHECK(VkContext->VulkanDevice != VK_NULL_HANDLE, "The VulkanDevice is no longer valid");

    for (EOS::TextureHandle handle : Textures)
    {
        VkContext->Destroy(handle);
    }

    vkDestroySwapchainKHR(VkContext->VulkanDevice, SwapChain, nullptr);

    for (const VkSemaphore& semaphore : AcquireSemaphores)
    {
        vkDestroySemaphore(VkContext->VulkanDevice, semaphore, nullptr);
    }
}

void VulkanSwapChain::Present(VkSemaphore waitSemaphore)
{
    const VkPresentInfoKHR presentInfo =
    {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &waitSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &SwapChain,
        .pImageIndices = &CurrentImageIndex,
    };

    const VkResult result = vkQueuePresentKHR(GraphicsQueue, &presentInfo);
    CHECK(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR, "Couldn't present the SwapChain image");

    GetNextImage = true;
    ++CurrentFrame;
}

VkImage VulkanSwapChain::GetCurrentImage() const
{
    CHECK(CurrentImageIndex < NumberOfSwapChainImages, "The Current Image Index is bigger then the amount of SwapChain images we have");
    return VkContext->TexturePool.Get(Textures[CurrentImageIndex])->Image;
}

VkImageView VulkanSwapChain::GetCurrentImageView() const
{
    CHECK(CurrentImageIndex < NumberOfSwapChainImages, "The Current Image Index is bigger then the amount of SwapChain images we have");
    return VkContext->TexturePool.Get(Textures[CurrentImageIndex])->ImageView;
}

uint32_t VulkanSwapChain::GetNumSwapChainImages() const
{
    return NumberOfSwapChainImages;
}

const VkSurfaceFormatKHR& VulkanSwapChain::GetFormat() const
{
    return SurfaceFormat;
}

EOS::TextureHandle VulkanSwapChain::GetCurrentTexture()
{
    GetAndWaitOnNextImage();
    CHECK(CurrentImageIndex < NumberOfSwapChainImages, "The Current Image Index is bigger then the amount of SwapChain images we have");
    return Textures[CurrentImageIndex];
}

void VulkanSwapChain::GetAndWaitOnNextImage()
{
    //Get The Next SwapChain Image
    if (GetNextImage)
    {
        const VkSemaphoreWaitInfo waitInfo =
        {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .semaphoreCount = 1,
            .pSemaphores = &VkContext->TimelineSemaphore,
            .pValues = &TimelineWaitValues[CurrentImageIndex],
        };

        // when timeout is set to UINT64_MAX, we wait until the next image has been acquired
        VK_ASSERT(vkWaitSemaphores(VkContext->VulkanDevice, &waitInfo, UINT64_MAX));

        VkSemaphore& acquireSemaphore = AcquireSemaphores[CurrentImageIndex];
        const VkResult result = vkAcquireNextImageKHR(VkContext->VulkanDevice, SwapChain, UINT64_MAX, acquireSemaphore, VK_NULL_HANDLE, &CurrentImageIndex);
        CHECK(result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR, "vkAcquireNextImageKHR Failed");

        GetNextImage = false;
        VkContext->VulkanCommandPool->WaitSemaphore(acquireSemaphore);
    }
}

VkSurfaceFormatKHR VulkanSwapChain::GetSwapChainFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats, EOS::ColorSpace desiredColorSpace)
{
    //TODO: Look into VkSurfaceFormat2KHR -> this enables Compression of the swapChain image
    //https://docs.vulkan.org/samples/latest/samples/performance/image_compression_control/README.html

    //Non Linear is the default
    VkSurfaceFormatKHR preferred{VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    if (desiredColorSpace == EOS::ColorSpace::SRGB_Linear)
    {
        // VK_COLOR_SPACE_BT709_LINEAR_EXT is the closest space to linear
        preferred  = VkSurfaceFormatKHR{VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_BT709_LINEAR_EXT};
    }

    //Check if we have a combination with our desired format & color space
    for (const VkSurfaceFormatKHR& fmt : availableFormats)
    {
        if (fmt.format == preferred.format && fmt.colorSpace == preferred.colorSpace) { return fmt; }
    }

    // if we can't find a matching format and color space, fallback on matching only format
    for (const VkSurfaceFormatKHR& fmt : availableFormats)
    {
        if (fmt.format == preferred.format) { return fmt; }
    }

    //If we still haven't found a format we just pick the first available option
    return availableFormats[0];
}


VulkanSwapChainSupportDetails::VulkanSwapChainSupportDetails(const VulkanContext& vulkanContext)
{
    //Get Surface Capabilities
    VK_ASSERT(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkanContext.VulkanPhysicalDevice, vulkanContext.VulkanSurface, &capabilities));

    //Get the Surface Support
    VK_ASSERT(vkGetPhysicalDeviceSurfaceSupportKHR(vulkanContext.VulkanPhysicalDevice, vulkanContext.VulkanDeviceQueues.Graphics.QueueFamilyIndex, vulkanContext.VulkanSurface, &queueFamilySupportsPresentation));

    //Get the available Surface Formats
    uint32_t formatCount;
    VK_ASSERT(vkGetPhysicalDeviceSurfaceFormatsKHR(vulkanContext.VulkanPhysicalDevice, vulkanContext.VulkanSurface, &formatCount, nullptr));
    formats.resize(formatCount);
    VK_ASSERT(vkGetPhysicalDeviceSurfaceFormatsKHR(vulkanContext.VulkanPhysicalDevice, vulkanContext.VulkanSurface, &formatCount, formats.data()));

    //Get the available Present Modes
    uint32_t presentModeCount;
    VK_ASSERT(vkGetPhysicalDeviceSurfacePresentModesKHR(vulkanContext.VulkanPhysicalDevice, vulkanContext.VulkanSurface, &presentModeCount, nullptr));
    presentModes.resize(presentModeCount);
    VK_ASSERT(vkGetPhysicalDeviceSurfacePresentModesKHR(vulkanContext.VulkanPhysicalDevice, vulkanContext.VulkanSurface, &presentModeCount, presentModes.data()));
}

CommandPool::CommandPool(const VkDevice &device, uint32_t queueIndex)
    : Device(device)
{
    WaitSemaphores.reserve(2);
    SignalSemaphores.reserve(2);

    vkGetDeviceQueue(device, queueIndex, 0, &Queue); //Store a copy of the Queue

    const VkCommandPoolCreateInfo createInfo =
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = queueIndex,
    };

    VK_ASSERT(vkCreateCommandPool(device, &createInfo, nullptr, &VulkanCommandPool));
    VK_ASSERT(VkDebug::SetDebugObjectName(device, VK_OBJECT_TYPE_COMMAND_POOL, reinterpret_cast<uint64_t>(VulkanCommandPool), "CommandPool"));

    const VkCommandBufferAllocateInfo allocateInfo =
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = VulkanCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    //Allocate buffers for each buffer in our pool
    for (uint32_t i{}; i < MaxCommandBuffers; ++i)
    {
        VK_ASSERT(vkAllocateCommandBuffers(device, &allocateInfo, &Buffers[i].VulkanCommandBufferAllocated));
        Buffers[i].Semaphore = VkSynchronization::CreateSemaphore(device, fmt::format("Semaphore of CommandBuffer: {}", i).c_str());
        Buffers[i].Fence = VkSynchronization::CreateFence(device, fmt::format("Fence of CommandBuffer: {}", i).c_str());
        Buffers[i].Handle.BufferIndex = i;

        ++NumberOfAvailableCommandBuffers;
    }
}

CommandPool::~CommandPool()
{
    //Wait until everything is processed
    WaitAll();

    //Destroy all data of the buffers
    for (CommandBufferData& buffer : Buffers)
    {
        vkDestroyFence(Device, buffer.Fence, nullptr);
        vkDestroySemaphore(Device, buffer.Semaphore, nullptr);
    }

    //Destroy the internal pool itself
    vkDestroyCommandPool(Device, VulkanCommandPool, nullptr);
}

void CommandPool::WaitSemaphore(const VkSemaphore& semaphore)
{
    CHECK(WaitOnSemaphore.semaphore == VK_NULL_HANDLE, "The wait Semaphore is not Empty");
    WaitOnSemaphore.semaphore = semaphore;
}

void CommandPool::WaitAll()
{
    VkFence fences[MaxCommandBuffers];
    uint32_t numFences = 0;

    for (const CommandBufferData& buffer : Buffers)
    {
        if (buffer.VulkanCommandBuffer != VK_NULL_HANDLE && !buffer.isEncoding)
        {
            fences[numFences++] = buffer.Fence;
        }
    }

    if (numFences)
    {
        VK_ASSERT(vkWaitForFences(Device, numFences, fences, VK_TRUE, UINT64_MAX));
    }

    TryResetCommandBuffers();
}

void CommandPool::Wait(const EOS::SubmitHandle handle)
{
    if (handle.Empty())
    {
        vkDeviceWaitIdle(Device);
        return;
    }

    if (IsReady(handle))
    {
        return;
    }

    const bool isEncoding = Buffers[handle.BufferIndex].isEncoding;
    CHECK(!isEncoding, "The buffer is not submitted yet, this should not be possible");
    if (isEncoding)
    {
        //This buffer has never been submitted, this should not be possible at this point.
        return;
    }

    VK_ASSERT(vkWaitForFences(Device, 1, &Buffers[handle.BufferIndex].Fence, VK_TRUE, UINT64_MAX));

    TryResetCommandBuffers();
}

void CommandPool::Signal(const VkSemaphore& semaphore, const uint64_t& signalValue)
{
    CHECK(semaphore != VK_NULL_HANDLE, "The passed semaphore parameter is not valid.");
    SignalSemaphore.semaphore = semaphore;
    SignalSemaphore.value = signalValue;
}

bool CommandPool::IsReady(EOS::SubmitHandle handle, bool fastCheck) const
{
    //If its a empty handle then its "ready"
    if (handle.Empty())
    {
        return true;
    }

    CHECK(handle.BufferIndex < MaxCommandBuffers, "The buffer index of the given handle is bigger then the MaxCommandBuffers.");

    const CommandBufferData& buffer = Buffers[handle.BufferIndex];

    //If the buffer is not in use.
    if (buffer.VulkanCommandBuffer == VK_NULL_HANDLE)
    {
        return true;
    }

    //If the handle to the buffer is no longer in use by the command buffer is was created for
    if (buffer.Handle.ID != handle.ID)
    {
        return true;
    }

    // Don't check the fence.
    if (fastCheck)
    {
        return false;
    }

    return vkWaitForFences(Device, 1, &buffer.Fence, VK_TRUE, 0) == VK_SUCCESS;
}

VkSemaphore CommandPool::AcquireLastSubmitSemaphore()
{
    return std::exchange(LastSubmitSemaphore.semaphore, VK_NULL_HANDLE);;
}

EOS::SubmitHandle CommandPool::Submit(CommandBufferData& data)
{
    CHECK(data.isEncoding, "The buffer you want to submit is not recording.");
    VK_ASSERT(vkEndCommandBuffer(data.VulkanCommandBuffer));

    //TODO instead of keeping members and them pushing them into vectors here.
    //Just Emplace the objects directly into the vector and remove the WaitOn Last Submit members
    WaitSemaphores.clear(); //TODO: double check clearing doesn't reduce memory
    if (WaitOnSemaphore.semaphore)
    {
        WaitSemaphores.emplace_back(WaitOnSemaphore);
    }
    if (LastSubmitSemaphore.semaphore)
    {
        WaitSemaphores.emplace_back(LastSubmitSemaphore);
    }

    SignalSemaphores.clear();
    SignalSemaphores.emplace_back(VkSemaphoreSubmitInfo
    {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = data.Semaphore,
        .stageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
    });

    if (SignalSemaphore.semaphore)
    {
        SignalSemaphores.emplace_back(SignalSemaphore);
    }

    const VkCommandBufferSubmitInfo bufferSubmitInfo =
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = data.VulkanCommandBuffer,
    };

    const VkSubmitInfo2 submitInfo =
    {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = static_cast<uint32_t>(WaitSemaphores.size()),
        .pWaitSemaphoreInfos = WaitSemaphores.data(),
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &bufferSubmitInfo,
        .signalSemaphoreInfoCount = static_cast<uint32_t>(SignalSemaphores.size()),
        .pSignalSemaphoreInfos = SignalSemaphores.data(),
    };

    VK_ASSERT(vkQueueSubmit2(Queue, 1, &submitInfo, data.Fence));

    LastSubmitSemaphore.semaphore = data.Semaphore;
    LastSubmitHandle = data.Handle;
    WaitOnSemaphore.semaphore = VK_NULL_HANDLE;
    SignalSemaphore.semaphore = VK_NULL_HANDLE;

    data.isEncoding = false;
    ++SubmitCounter;

    // skip the 0 value when uint32_t wraps around.
    if (!SubmitCounter) { ++SubmitCounter; }

    return LastSubmitHandle;
}

EOS::SubmitHandle CommandPool::GetNextSubmitHandle() const
{
    return NextSubmitHandle;
}

CommandBufferData* CommandPool::AcquireCommandBuffer()
{
    //Try to free a command buffer of none are free
    if (!NumberOfAvailableCommandBuffers)
    {
        TryResetCommandBuffers();
    }

    // if there is still no commandbuffer free in the pool, we will wait until one becomes available
    while (!NumberOfAvailableCommandBuffers)
    {
        EOS::Logger->warn("Waiting for a command buffer that is free to use...");
        TryResetCommandBuffers();
    }

    CommandBufferData* currentCommandBuffer = nullptr;

    // we are ok with any available buffer
    for (CommandBufferData& buffer : Buffers)
    {
        if (buffer.VulkanCommandBuffer == VK_NULL_HANDLE)
        {
            //TODO: This needs to be a copy, otherwise it will be moved out of the Buffers?
            currentCommandBuffer = &buffer;
            break;
        }
    }
    CHECK(NumberOfAvailableCommandBuffers, "No command buffers where available");
    CHECK(currentCommandBuffer, "No command buffers where available");
    CHECK(currentCommandBuffer->VulkanCommandBufferAllocated != VK_NULL_HANDLE, "No command buffers where available");

    currentCommandBuffer->Handle.ID = SubmitCounter;
    NumberOfAvailableCommandBuffers--;

    currentCommandBuffer->VulkanCommandBuffer = currentCommandBuffer->VulkanCommandBufferAllocated;
    currentCommandBuffer->isEncoding = true;

    constexpr VkCommandBufferBeginInfo beginInfo =
    {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VK_ASSERT(vkBeginCommandBuffer(currentCommandBuffer->VulkanCommandBuffer, &beginInfo));

    NextSubmitHandle = currentCommandBuffer->Handle;

    return currentCommandBuffer;
}

void CommandPool::TryResetCommandBuffers()
{
    for (CommandBufferData& buffer : Buffers)
    {
        if (buffer.VulkanCommandBuffer == VK_NULL_HANDLE || buffer.isEncoding)
        {
            continue;
        }

        const VkResult result = vkWaitForFences(Device, 1, &buffer.Fence, VK_TRUE, 0);

        if (result == VK_SUCCESS)
        {
            VK_ASSERT(vkResetCommandBuffer(buffer.VulkanCommandBuffer, VkCommandBufferResetFlags{0}));
            VK_ASSERT(vkResetFences(Device, 1, &buffer.Fence));
            buffer.VulkanCommandBuffer = VK_NULL_HANDLE;
            ++NumberOfAvailableCommandBuffers;
        }
        else if (result != VK_TIMEOUT)
        {
            VK_ASSERT(result);
        }
    }
}

CommandBuffer::CommandBuffer(VulkanContext *vulkanContext)
: CommandBufferImpl((vulkanContext->VulkanCommandPool->AcquireCommandBuffer()))
, VkContext(vulkanContext)
{
}

CommandBuffer & CommandBuffer::operator=(CommandBuffer &&other) noexcept
{
    if (this != &other)
    {
        VkContext = std::exchange(other.VkContext, nullptr);
        CommandBufferImpl = std::exchange(other.CommandBufferImpl, {});
        LastSubmitHandle = std::exchange(other.LastSubmitHandle, {});
    }
    return *this;
}

CommandBuffer::operator bool() const
{
    return VkContext != nullptr;
}

VulkanContext::VulkanContext(const EOS::ContextCreationDescription& contextDescription)
: Configuration(contextDescription.config)
{
    CHECK(volkInitialize() == VK_SUCCESS, "Failed to Initialize VOLK");

    CreateVulkanInstance(contextDescription.applicationName);
    SetupDebugMessenger();
    CreateSurface(contextDescription.window, contextDescription.display);

    //Select the Physical Device
    std::vector<EOS::HardwareDeviceDescription> hardwareDevices;
    GetHardwareDevice(contextDescription.preferredHardwareType, hardwareDevices);
    VkContext::SelectHardwareDevice(hardwareDevices, VulkanPhysicalDevice);

    //Create our Vulkan Device
    VkContext::CreateVulkanDevice(VulkanDevice, VulkanPhysicalDevice, VulkanDeviceQueues);

    //Create SwapChain
    //TODO: will it need a description struct?
    VulkanSwapChainCreationDescription desc
    {
        .vulkanContext = this,
        .width = 100,
        .height = 80,
    };

    SwapChain = std::make_unique<VulkanSwapChain>(desc);


    //Create our Timeline Semaphore
    TimelineSemaphore = VkSynchronization::CreateSemaphoreTimeline(VulkanDevice, SwapChain->GetNumSwapChainImages() - 1, "Semaphore: TimelineSemaphore");

    //Create our CommandPool
    VulkanCommandPool = std::make_unique<CommandPool>(VulkanDevice, VulkanDeviceQueues.Graphics.QueueFamilyIndex);


    //TODO: pipeline cache

    //TODO: VMA init

    //TODO: Staging Device
}

VulkanContext::~VulkanContext()
{
    //Wait unit all work has been done
    VK_ASSERT(vkDeviceWaitIdle(VulkanDevice));

    SwapChain.reset(nullptr);

    vkDestroySemaphore(VulkanDevice, TimelineSemaphore, nullptr);

    if (TexturePool.NumObjects())
    {
        EOS::Logger->error("{} Leaked textures", TexturePool.NumObjects());
    }
    TexturePool.Clear();

    if (ShaderModulePool.NumObjects())
    {
        EOS::Logger->error("{} Leaked Shader Modules", ShaderModulePool.NumObjects());
    }
    ShaderModulePool.Clear();

    WaitOnDeferredTasks();

    VulkanCommandPool.reset(nullptr);

    vkDestroySurfaceKHR(VulkanInstance, VulkanSurface, nullptr);

    //vmaDestroyAllocator(Vma);

    vkDestroyDevice(VulkanDevice, nullptr);
    vkDestroyDebugUtilsMessengerEXT(VulkanInstance, VulkanDebugMessenger, nullptr);
    vkDestroyInstance(VulkanInstance, nullptr);
}

EOS::ICommandBuffer& VulkanContext::AcquireCommandBuffer()
{
    CHECK(!CurrentCommandBuffer, "Another CommandBuffer has been acquired this frame");
    CurrentCommandBuffer = std::move(CommandBuffer{this});
    return CurrentCommandBuffer;
}

EOS::SubmitHandle VulkanContext::Submit(EOS::ICommandBuffer &commandBuffer, EOS::TextureHandle present)
{
    CommandBuffer* vkCmdBuffer = dynamic_cast<CommandBuffer*>(&commandBuffer);
    CHECK(vkCmdBuffer, "The command buffer is not valid");

#if defined(EOS_DEBUG)
    if (present)
    {
        const VulkanImage& swapChainTextures = *TexturePool.Get(present);
        CHECK(VulkanImage::IsSwapChainImage(swapChainTextures), "The passed present texture handle is not from a SwapChain");
    }
#endif

    const bool shouldPresent = HasSwapChain() && present;

    //If we a presenting a SwapChain image, signal our timeline semaphore
    if (shouldPresent)
    {
        //Create a unique Signal Value
        const uint64_t signalValue = SwapChain->CurrentFrame + SwapChain->NumberOfSwapChainImages;

        //Wait for this value next time we want to acquire this SwapChain image
        SwapChain->TimelineWaitValues[SwapChain->CurrentImageIndex] = signalValue;
        VulkanCommandPool->Signal(TimelineSemaphore, signalValue);
    }

    vkCmdBuffer->LastSubmitHandle = VulkanCommandPool->Submit(*vkCmdBuffer->CommandBufferImpl);

    if (shouldPresent)
    {
        SwapChain->Present(VulkanCommandPool->AcquireLastSubmitSemaphore());
    }

    ProcessDeferredTasks();

    const EOS::SubmitHandle handle = vkCmdBuffer->LastSubmitHandle;

    //Reset the Command Buffer
    CurrentCommandBuffer = {};

    return handle;
}

EOS::TextureHandle VulkanContext::GetSwapChainTexture()
{
    CHECK(HasSwapChain(), "You dont have a SwapChain");
    if (!HasSwapChain())
    {
       EOS::Logger->error("No SwapChain Found");
    }

    EOS::TextureHandle swapChainTexture = SwapChain->GetCurrentTexture();
    CHECK(swapChainTexture.Valid(), "The SwapChain texture is not valid.");
    CHECK(TexturePool.Get(swapChainTexture)->ImageFormat != VK_FORMAT_UNDEFINED, "Invalid image format");

    return swapChainTexture;
}

EOS::Holder<EOS::ShaderModuleHandle> VulkanContext::CreateShaderModule(const EOS::ShaderInfo &shaderInfo)
{
    VkShaderModule vkShaderModule = VK_NULL_HANDLE;

    const VkShaderModuleCreateInfo createInfo =
    {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shaderInfo.spirv.size() * sizeof(uint32_t),
        .pCode = shaderInfo.spirv.data(),
    };

    VK_ASSERT(vkCreateShaderModule(VulkanDevice, &createInfo, nullptr, &vkShaderModule);)
    CHECK(vkShaderModule != VK_NULL_HANDLE, "Failed to create shader module from ShaderInfo");

    VK_ASSERT(VkDebug::SetDebugObjectName(VulkanDevice, VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<uint64_t>(vkShaderModule), shaderInfo.debugName));

    VulkanShaderModuleState state
    {
        .ShaderModule = vkShaderModule,
        .PushConstantsSize = shaderInfo.pushConstantSize
    };

    return {this, ShaderModulePool.Create(std::move(state))};
}

void VulkanContext::Destroy(EOS::TextureHandle handle)
{
    VulkanImage* image = TexturePool.Get(handle);
    CHECK(image, "Trying to destroy a already destroyed vulkan image");
    if (!image)
    {
        return;
    }

    Defer(std::packaged_task<void()>([device = VulkanDevice, imageView = image->ImageView]() { vkDestroyImageView(device, imageView, nullptr); }));

    if (image->ImageViewStorage)
    {
        Defer(std::packaged_task<void()>([device = VulkanDevice, imageView = image->ImageViewStorage]() { vkDestroyImageView(device, imageView, nullptr); }));
    }

    for (size_t i{}; i != VulkanImage::MaxMipLevels; ++i)
    {
        for (size_t j = 0; j != ARRAY_COUNT(image->ImageViewForFramebuffer[0]); ++j)
        {
            VkImageView v = image->ImageViewForFramebuffer[i][j];
            if (v != VK_NULL_HANDLE)
            {
                Defer(std::packaged_task<void()>([device = VulkanDevice, imageView = v]() { vkDestroyImageView(device, imageView, nullptr); }));
            }
        }
    }

    if (!image->IsOwningImage)
    {
        TexturePool.Destroy(handle);
        return;
    }

    //TODO: Add once VMA is setup and image has allocations on the GPU
    //if (image->MappedPtr)
    //{
    //    vmaUnmapMemory(static_cast<VmaAllocator>(GetVmaAllocator()), image->Allocation);
    //}

    //Defer(std::packaged_task<void()>([vma = GetVmaAllocator(), image = image->Image, allocation = image->VmaAllocation]() {vmaDestroyImage(static_cast<VmaAllocator>(vma), image, allocation);}));
    //Defer(std::packaged_task<void()>([device = VulkanDevice, image = image->Image, memory0 = image->VkMemory[0], memory1 = image->VkMemory[1], memory2 = image->VkMemory[2]]()
    //{
    //    vkDestroyImage(device, image, nullptr);
    //    if (memory0 != VK_NULL_HANDLE)
    //    {
    //        vkFreeMemory(device, memory0, nullptr);
    //    }

    //    if (memory1 != VK_NULL_HANDLE)
    //    {
    //        vkFreeMemory(device, memory1, nullptr);
    //    }

    //    if (memory2 != VK_NULL_HANDLE)
    //    {
    //        vkFreeMemory(device, memory2, nullptr);
    //    }
    //}));

    TexturePool.Destroy(handle);
}

void VulkanContext::Destroy(EOS::ShaderModuleHandle handle)
{
    const VulkanShaderModuleState* state = ShaderModulePool.Get(handle);

    if (!state) { return; }

    if (state->ShaderModule != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(VulkanDevice, state->ShaderModule, nullptr);
    }

    ShaderModulePool.Destroy(handle);
}

void VulkanContext::ProcessDeferredTasks() const
{
    while (!DeferredTasks.empty() && VulkanCommandPool->IsReady(DeferredTasks.front().Handle, true))
    {
        //Execute the deferred task
        DeferredTasks.front().Task();

        //Delete the deferred task
        DeferredTasks.pop_front();
    }
}

//Defer something until after a commandBuffer was submitted to the GPU
void VulkanContext::Defer(std::packaged_task<void()>&& task, EOS::SubmitHandle handle) const
{
    if (handle.Empty())
    {
        handle = VulkanCommandPool->GetNextSubmitHandle();
    }

    DeferredTasks.emplace_back(std::move(task), handle);
}


bool VulkanContext::HasSwapChain() const noexcept
{
    return SwapChain != nullptr;
}

void VulkanContext::CreateVulkanInstance(const char* applicationName)
{
    uint32_t apiVersion{};
    vkEnumerateInstanceVersion(&apiVersion);
    const VkApplicationInfo applicationInfo
    {
        .pApplicationName   = applicationName,
        .applicationVersion = VK_MAKE_VERSION(0, 0, 1),
        .pEngineName        = "EOS",
        .engineVersion      = VK_MAKE_VERSION(0, 0, 1),
        .apiVersion         = apiVersion,
    };

    //Check if we can use Validation Layers
    uint32_t numberOfLayers{};
    vkEnumerateInstanceLayerProperties(&numberOfLayers, nullptr);
    std::vector<VkLayerProperties> layerProperties(numberOfLayers);
    vkEnumerateInstanceLayerProperties(&numberOfLayers, layerProperties.data());
    bool foundLayer = false;
    for (const VkLayerProperties& props : layerProperties)
    {
        if (strcmp(props.layerName, validationLayer) == 0)
        {
            foundLayer = true;
            break;
        }
    }
    Configuration.enableValidationLayers = foundLayer;

    //Setup the validation layers and extensions
    uint32_t instanceExtensionCount;
    VK_ASSERT(vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, nullptr));
    std::vector<VkExtensionProperties> allInstanceExtensions(instanceExtensionCount);
    VK_ASSERT(vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtensionCount, allInstanceExtensions.data()));

    std::vector<VkValidationFeatureEnableEXT> validationFeatures;
    validationFeatures.reserve(2);

    std::vector<const char*> instanceExtensionNames;
    std::vector<const char*> availableInstanceExtensionNames;
    instanceExtensionNames.reserve(4);
    availableInstanceExtensionNames.reserve(5);

    if (Configuration.enableValidationLayers)
    {
        validationFeatures.emplace_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_EXT);
        validationFeatures.emplace_back(VK_VALIDATION_FEATURE_ENABLE_GPU_ASSISTED_RESERVE_BINDING_SLOT_EXT);
        instanceExtensionNames.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    instanceExtensionNames.emplace_back(VK_KHR_SURFACE_EXTENSION_NAME);

    //Choose the right surface extension
    #if defined(EOS_PLATFORM_WINDOWS)
            instanceExtensionNames.emplace_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
    #elif defined(EOS_PLATFORM_WAYLAND)
            instanceExtensionNames.emplace_back(VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME);
    #elif defined(EOS_PLATFORM_X11)
            instanceExtensionNames.emplace_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
    #endif

    EOS::Logger->debug("Vulkan Instance Extensions:\n     {}", fmt::join(allInstanceExtensions | std::views::transform([](const auto& ext) { return ext.extensionName; }), "\n     "));

    bool foundInstanceExtension = false;
    for (const auto& instanceExtensionName : instanceExtensionNames)
    {
        foundInstanceExtension = false;
        for (const auto&[extensionName, specVersion] : allInstanceExtensions)
        {
            if (strcmp(extensionName, instanceExtensionName) == 0)
            {
                foundInstanceExtension = true;
                availableInstanceExtensionNames.emplace_back(instanceExtensionName);
                break;
            }
        }

        if (!foundInstanceExtension)
        {
            EOS::Logger->warn("{} -> Is not available on your device.", instanceExtensionName);
        }
    }

    VkValidationFeaturesEXT features =
    {
        .sType                          = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT,
        .pNext                          = nullptr,
        .enabledValidationFeatureCount  = static_cast<uint32_t>(validationFeatures.size()),
        .pEnabledValidationFeatures     = validationFeatures.data()
    };

    VkBool32 fine_grained_locking{VK_TRUE};
    VkBool32 validate_core{VK_TRUE};
    VkBool32 check_image_layout{VK_TRUE};
    VkBool32 check_command_buffer{VK_TRUE};
    VkBool32 check_object_in_use{VK_TRUE};
    VkBool32 check_query{VK_TRUE};
    VkBool32 check_shaders{VK_TRUE};
    VkBool32 check_shaders_caching{VK_TRUE};
    VkBool32 unique_handles{VK_TRUE};
    VkBool32 object_lifetime{VK_TRUE};
    VkBool32 stateless_param{VK_TRUE};
    std::vector<const char*> debug_action{"VK_DBG_LAYER_ACTION_LOG_MSG", "VK_DBG_LAYER_ACTION_BREAK"};  // "VK_DBG_LAYER_ACTION_DEBUG_OUTPUT", //TODO: Make Creation Option to break on error
    std::vector<const char*> report_flags{"error"};
    std::vector<VkLayerSettingEXT> layerSettings
    {
            {validationLayer, "fine_grained_locking", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &fine_grained_locking},
            {validationLayer, "validate_core", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &validate_core},
            {validationLayer, "check_image_layout", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &check_image_layout},
            {validationLayer, "check_command_buffer", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &check_command_buffer},
            {validationLayer, "check_object_in_use", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &check_object_in_use},
            {validationLayer, "check_query", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &check_query},
            {validationLayer, "check_shaders", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &check_shaders},
            {validationLayer, "check_shaders_caching", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &check_shaders_caching},
            {validationLayer, "unique_handles", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &unique_handles},
            {validationLayer, "object_lifetime", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &object_lifetime},
            {validationLayer, "stateless_param", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &stateless_param},
            {validationLayer, "debug_action", VK_LAYER_SETTING_TYPE_STRING_EXT, static_cast<uint32_t>(debug_action.size()), debug_action.data()},
            {validationLayer, "report_flags", VK_LAYER_SETTING_TYPE_STRING_EXT, static_cast<uint32_t>(report_flags.size()), report_flags.data()},
    };

    VkLayerSettingsCreateInfoEXT layerSettingsCreateInfo =
    {
        .sType        = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
        .pNext        = &features,
        .settingCount = static_cast<uint32_t>(layerSettings.size()),
        .pSettings    = layerSettings.data(),
    };

    constexpr VkInstanceCreateFlags flags = 0;
    const VkInstanceCreateInfo instanceCreateInfo
    {
        .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext                   = &layerSettingsCreateInfo,
        .flags                   = flags,
        .pApplicationInfo        = &applicationInfo,
        .enabledLayerCount       = 1,
        .ppEnabledLayerNames     = &validationLayer,
        .enabledExtensionCount   = static_cast<uint32_t>(availableInstanceExtensionNames.size()),
        .ppEnabledExtensionNames = availableInstanceExtensionNames.data(),
    };

    // Actual Vulkan instance creation
    VK_ASSERT(vkCreateInstance(&instanceCreateInfo, nullptr, &VulkanInstance));

    //Load the volk Instance
    volkLoadInstance(VulkanInstance);
}

void VulkanContext::SetupDebugMessenger()
{
   const VkDebugUtilsMessengerCreateInfoEXT debugUtilMessengerCreateInfo =
    {
       .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
       .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
       .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
       .pfnUserCallback = &VkDebug::DebugCallback,
       .pUserData = this,
   };

   VK_ASSERT(vkCreateDebugUtilsMessengerEXT(VulkanInstance, &debugUtilMessengerCreateInfo, nullptr, &VulkanDebugMessenger));
}

void VulkanContext::CreateSurface(void* window, [[maybe_unused]] void* display)
{
#if defined(EOS_PLATFORM_WINDOWS)
    const VkWin32SurfaceCreateInfoKHR SurfaceCreateInfo =
    {
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .hinstance = GetModuleHandle(nullptr),
        .hwnd = static_cast<HWND>(window),
    };
    VK_ASSERT(vkCreateWin32SurfaceKHR(VulkanInstance, &SurfaceCreateInfo, nullptr, &VulkanSurface));
#elif defined(EOS_PLATFORM_X11)
    const VkXlibSurfaceCreateInfoKHR SurfaceCreateInfo =
    {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .flags = 0,
        .dpy = static_cast<Display*>(display),
        .window = reinterpret_cast<Window>(window),
    };
    VK_ASSERT(vkCreateXlibSurfaceKHR(VulkanInstance, &SurfaceCreateInfo, nullptr, &VulkanSurface));
#elif defined(EOS_PLATFORM_WAYLAND)
    const VkWaylandSurfaceCreateInfoKHR SurfaceCreateInfo =
    {
        .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .flags = 0,
        .display = static_cast<wl_display*>(display),
        .surface = static_cast<wl_surface*>(window),
    };
    VK_ASSERT(vkCreateWaylandSurfaceKHR(VulkanInstance, &SurfaceCreateInfo, nullptr, &VulkanSurface));
#endif
}

void VulkanContext::GetHardwareDevice(EOS::HardwareDeviceType desiredDeviceType, std::vector<EOS::HardwareDeviceDescription>& compatibleDevices) const
{
    uint32_t deviceCount = 0;
    VK_ASSERT(vkEnumeratePhysicalDevices(VulkanInstance, &deviceCount, nullptr));
    std::vector<VkPhysicalDevice> hardwareDevices(deviceCount);
    VK_ASSERT(vkEnumeratePhysicalDevices(VulkanInstance, &deviceCount, hardwareDevices.data()));

    for (VkPhysicalDevice& hardwareDevice : hardwareDevices)
    {
        VkPhysicalDeviceProperties deviceProperties;
        vkGetPhysicalDeviceProperties(hardwareDevice, &deviceProperties);
        const auto deviceType = static_cast<EOS::HardwareDeviceType>(deviceProperties.deviceType);

        if (desiredDeviceType != EOS::HardwareDeviceType::Software && desiredDeviceType != deviceType) { continue; }

        //Convert the device to a unsigned (long) int (size dependant on building for 32 or 64 bit) and use that as the GUID of the physical device.
        compatibleDevices.emplace_back(reinterpret_cast<uintptr_t>(hardwareDevice), deviceType, deviceProperties.deviceName);
    }

    CHECK(!hardwareDevices.empty(), "Couldn't find a physical hardware device!");
}

void VulkanContext::WaitOnDeferredTasks()
{
    for (auto& task : DeferredTasks)
    {
        VulkanCommandPool->Wait(task.Handle);
        task.Task();
    }

    DeferredTasks.clear();
}

bool VulkanContext::IsHostVisibleMemorySingleHeap() const
{
    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(VulkanPhysicalDevice, &memoryProperties);

    if (memoryProperties.memoryHeapCount != 1) { return false; }

    constexpr uint32_t checkFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    const bool hasMemoryType = std::ranges::any_of(memoryProperties.memoryTypes,[checkFlags](const auto& memoryType)
    {
        return (memoryType.propertyFlags & checkFlags) == checkFlags;
    });

    return hasMemoryType;
}
