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

void cmdBeginRendering(EOS::ICommandBuffer &commandBuffer, const EOS::RenderPass &renderPass, EOS::Framebuffer &description, const EOS::Dependencies &dependancies)
{
    CommandBuffer* vulkanCommandBuffer = dynamic_cast<CommandBuffer*>(&commandBuffer);

    CHECK(!vulkanCommandBuffer->IsRendering, "Make sure you call cmdEndRendering before calling cmdBeginRendering again");
    vulkanCommandBuffer->IsRendering = true;

    const uint32_t numberOfFrameBufferColorAttachments = description.GetNumColorAttachments();
    const uint32_t numberOfPassColorAttachments = renderPass.GetNumColorAttachments();

    CHECK(numberOfFrameBufferColorAttachments == numberOfPassColorAttachments, "Make sure that the amount of described color attachments in the renderpass and framebuffer are the same.");
    vulkanCommandBuffer->VulkanFrameBuffer = std::move(description);


    VulkanTexturePool& texturePool = vulkanCommandBuffer->VkContext->TexturePool;

    //Handle Rendering Attachements
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    uint32_t mipLevel = 0;
    uint32_t frameBufferWidth = 0;
    uint32_t frameBufferHeight = 0;
    VkRenderingAttachmentInfo colorAttachments[EOS_MAX_COLOR_ATTACHMENTS];
    for (uint32_t i{}; i != numberOfPassColorAttachments; ++i)
    {
        const EOS::Framebuffer::AttachmentDesc& attachment = vulkanCommandBuffer->VulkanFrameBuffer.Color[i];
        CHECK(!attachment.Texture.Empty(), "The passed color attachement is empty");

        VulkanImage& colorTexture = *texturePool.Get(attachment.Texture);
        const EOS::RenderPass::AttachmentDesc& descColor = renderPass.Color[i];
        const VkExtent3D dimension = colorTexture.Extent;

        //Check levels
        if (mipLevel && descColor.Level)
        {
            CHECK(descColor.Level == mipLevel, "All color attachments should have the same mip-level");
        }

        //Check attachment width
        if (frameBufferWidth)
        {
            CHECK(dimension.width == frameBufferWidth, "All attachments should have the same width");
        }

        //Check attachement height
        if (frameBufferHeight)
        {
            CHECK(dimension.height == frameBufferHeight, "All attachments should have the same height");
        }

        mipLevel = descColor.Level;
        frameBufferWidth = dimension.width;
        frameBufferHeight = dimension.height;
        samples = colorTexture.Samples;

        colorAttachments[i] =
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext = nullptr,
            .imageView = colorTexture.GetImageViewForFramebuffer(vulkanCommandBuffer->VkContext->GetDevice(), descColor.Level, descColor.Layer),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = (samples > 1) ? VK_RESOLVE_MODE_AVERAGE_BIT : VK_RESOLVE_MODE_NONE,
            .resolveImageView = VK_NULL_HANDLE,
            .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp = VkContext::LoadOpToVkAttachmentLoadOp(descColor.LoadOpState),
            .storeOp = VkContext::StoreOpToVkAttachmentStoreOp(descColor.StoreOpState),
            .clearValue = {.color = {.float32 = {descColor.ClearColor[0], descColor.ClearColor[1], descColor.ClearColor[2], descColor.ClearColor[3]}}},
        };

        // handle MSAA
        if (descColor.StoreOpState == EOS::StoreOp::MsaaResolve)
        {
            CHECK(samples > 1, "A MSAA Resolve should have more then 1 sample");
            CHECK(!attachment.ResolveTexture.Empty(), "Framebuffer attachment should contain a resolve texture");
            VulkanImage& colorResolveTexture = *texturePool.Get(attachment.ResolveTexture);
            colorAttachments[i].resolveImageView = colorResolveTexture.GetImageViewForFramebuffer(vulkanCommandBuffer->VkContext->GetDevice(), descColor.Level, descColor.Layer);
            colorAttachments[i].resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
    }

    //Handle the depth attachment
    VkRenderingAttachmentInfo depthAttachment{};
    EOS::TextureHandle depthTextureHandle = vulkanCommandBuffer->VulkanFrameBuffer.DepthStencil.Texture;
    if (depthTextureHandle)
    {
        VulkanImage& depthTexture = *texturePool.Get(vulkanCommandBuffer->VulkanFrameBuffer.DepthStencil.Texture);
        const EOS::RenderPass::AttachmentDesc& descDepth = renderPass.Depth;
        CHECK(descDepth.Level == mipLevel, "Depth attachment should have the same mip-level as color attachments");

        depthAttachment =
        {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext = nullptr,
            .imageView = depthTexture.GetImageViewForFramebuffer(vulkanCommandBuffer->VkContext->GetDevice(), descDepth.Level, descDepth.Layer),
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_NONE,
            .resolveImageView = VK_NULL_HANDLE,
            .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp = VkContext::LoadOpToVkAttachmentLoadOp(descDepth.LoadOpState),
            .storeOp = VkContext::StoreOpToVkAttachmentStoreOp(descDepth.StoreOpState),
            .clearValue = {.depthStencil = {.depth = descDepth.ClearDepth, .stencil = descDepth.ClearStencil}},
        };

        // handle depth MSAA
        if (descDepth.StoreOpState == EOS::StoreOp::MsaaResolve)
        {
            CHECK(depthTexture.Samples == samples, "You need the same amount of samples for you depth texture.");
            const EOS::Framebuffer::AttachmentDesc& attachment = vulkanCommandBuffer->VulkanFrameBuffer.DepthStencil;
            CHECK(!attachment.ResolveTexture.Empty(), "Framebuffer depth attachment should contain a resolve texture");

            VulkanImage& depthResolveTexture = *texturePool.Get(attachment.ResolveTexture);
            depthAttachment.resolveImageView = depthResolveTexture.GetImageViewForFramebuffer(vulkanCommandBuffer->VkContext->GetDevice(), descDepth.Level, descDepth.Layer);
            depthAttachment.resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        }

        const VkExtent3D dim = depthTexture.Extent;
        if (frameBufferWidth)
        {
            CHECK(dim.width == frameBufferWidth, "All attachments should have the same width");
        }

        if (frameBufferHeight)
        {
            CHECK(dim.height == frameBufferHeight, "All attachments should have the same height");
        }

        mipLevel = descDepth.Level;
        frameBufferWidth = dim.width;
        frameBufferHeight = dim.height;
    }

    const uint32_t width = std::max(frameBufferWidth >> mipLevel, 1u);
    const uint32_t height = std::max(frameBufferHeight >> mipLevel, 1u);

    const EOS::Viewport viewport = {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, +1.0f};
    VkRenderingAttachmentInfo stencilAttachment = depthAttachment;

    const bool isStencilFormat = renderPass.Stencil.LoadOpState != EOS::LoadOp::Invalid;
    const VkRenderingInfo renderingInfo
    {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .pNext = nullptr,
      .flags = 0,
      .renderArea = {VkOffset2D{0u, 0u}, VkExtent2D{width, height}},
      .layerCount = 1,
      .viewMask = 0,
      .colorAttachmentCount = numberOfPassColorAttachments,
      .pColorAttachments = colorAttachments,
      .pDepthAttachment = depthTextureHandle ? &depthAttachment : nullptr,
      .pStencilAttachment = isStencilFormat ? &stencilAttachment : nullptr,
    };

    // Set the viewport
    const VkViewport vp
    {
        .x = viewport.X,
        .y = viewport.Height - viewport.Y,
        .width = viewport.Width,
        .height = -viewport.Height,
        .minDepth = viewport.MinDepth,
        .maxDepth = viewport.MaxDepth,
    };
    vkCmdSetViewport(vulkanCommandBuffer->CommandBufferImpl->VulkanCommandBuffer, 0, 1, &vp);

    // Set the scissors
    const VkRect2D scissor
    {
        VkOffset2D{static_cast<int32_t>(viewport.X), static_cast<int32_t>(viewport.Y)},
        VkExtent2D{static_cast<unsigned int>(viewport.Width), static_cast<unsigned int>(viewport.Height)},
    };
    vkCmdSetScissor(vulkanCommandBuffer->CommandBufferImpl->VulkanCommandBuffer, 0, 1, &scissor);

    // Reset the Depth states
    EOS::DepthState state{};
    cmdSetDepthState(commandBuffer, state);

    //CheckAndUpdateDescriptorSets(); -> TODO: this is a solution but we can avoid to check this every frame, what about we add any descriptor sets...
    vkCmdSetDepthBiasEnable(vulkanCommandBuffer->CommandBufferImpl->VulkanCommandBuffer, VK_FALSE);
    vkCmdBeginRendering(vulkanCommandBuffer->CommandBufferImpl->VulkanCommandBuffer, &renderingInfo);
}

void cmdEndRendering(EOS::ICommandBuffer &commandBuffer)
{
    CommandBuffer* vulkanCommandBuffer = dynamic_cast<CommandBuffer*>(&commandBuffer);
    CHECK(vulkanCommandBuffer->IsRendering, "Cannot end rendering if it hasn't started yet.");
    vulkanCommandBuffer->IsRendering = false;
    vulkanCommandBuffer->LastPipelineBound = nullptr;

    vkCmdEndRendering(vulkanCommandBuffer->CommandBufferImpl->VulkanCommandBuffer);

    EOS::Framebuffer& frambuffer = vulkanCommandBuffer->VulkanFrameBuffer;
    frambuffer = {};
}

void cmdBindRenderPipeline(EOS::ICommandBuffer &commandBuffer, EOS::RenderPipelineHandle renderPipelineHandle)
{
    CHECK(!renderPipelineHandle.Empty(), "The passed renderPipelineHandle needs to be a valid handle");
    CommandBuffer* vulkanCommandBuffer = dynamic_cast<CommandBuffer*>(&commandBuffer);

    vulkanCommandBuffer->CurrentGraphicsPipeline = std::move(renderPipelineHandle);
    vulkanCommandBuffer->CurrentComputePipeline = {};
    vulkanCommandBuffer->CurrentRayTracingPipeline = {};

    VulkanRenderPipelinePool& renderPipelinePool = vulkanCommandBuffer->VkContext->RenderPipelinePool;
    const VulkanRenderPipelineState* rps = renderPipelinePool.Get(vulkanCommandBuffer->CurrentGraphicsPipeline);
    CHECK(rps, "The resolved RenderPipeline State is not valid");

    const bool hasDepthAttachmentPipeline = rps->Description.DepthFormat != EOS::Format::Invalid;
    const bool hasDepthAttachmentPass = !vulkanCommandBuffer->VulkanFrameBuffer.DepthStencil.Texture.Empty();
    CHECK(hasDepthAttachmentPipeline == hasDepthAttachmentPass, "Make sure your render pass and render pipeline both have matching depth attachments");
    CHECK(rps->Pipeline != VK_NULL_HANDLE, "The Vulkan Pipeline is a NULL Handle, It did not got created");

    //TODO: make sure this this is if is not needed,
    //We can sort the render loop based on pipelines...
    if (vulkanCommandBuffer->LastPipelineBound != rps->Pipeline)
    {
        vulkanCommandBuffer->LastPipelineBound = rps->Pipeline;
        vkCmdBindPipeline(vulkanCommandBuffer->CommandBufferImpl->VulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rps->Pipeline);
        vulkanCommandBuffer->VkContext->BindDefaultDescriptorSet(vulkanCommandBuffer->CommandBufferImpl->VulkanCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, rps->PipelineLayout);
    }
}

void cmdBindVertexBuffer(const EOS::ICommandBuffer& commandBuffer, uint32_t index, EOS::BufferHandle buffer, const uint64_t bufferOffset)
{
    CHECK(!buffer.Empty(), "The handle to the buffer is empty");
    const CommandBuffer* vulkanCommandBuffer = dynamic_cast<const CommandBuffer*>(&commandBuffer);
    VulkanContext* vkContext = vulkanCommandBuffer->VkContext;

    const VulkanBuffer* vkbuffer = vkContext->BufferPool.Get(buffer);
    CHECK(vkbuffer->VkUsageFlags & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "The buffer usage flags do not indicate this is a Vertex buffer.");

    vkCmdBindVertexBuffers(vulkanCommandBuffer->CommandBufferImpl->VulkanCommandBuffer, index, 1, &vkbuffer->VulkanVkBuffer, &bufferOffset);
}

void cmdBindIndexBuffer(const EOS::ICommandBuffer &commandBuffer, EOS::BufferHandle indexBuffer, EOS::IndexFormat indexFormat, uint64_t indexBufferOffset)
{
    CHECK(!indexBuffer.Empty(), "The handle to the buffer is empty");
    const CommandBuffer* vulkanCommandBuffer = dynamic_cast<const CommandBuffer*>(&commandBuffer);
    VulkanContext* vkContext = vulkanCommandBuffer->VkContext;

    const VulkanBuffer* vkbuffer = vkContext->BufferPool.Get(indexBuffer);
    CHECK(vkbuffer->VkUsageFlags & VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "The buffer usage flags do not indicate this is a Index buffer.");
    
    vkCmdBindIndexBuffer(vulkanCommandBuffer->CommandBufferImpl->VulkanCommandBuffer, vkbuffer->VulkanVkBuffer, indexBufferOffset, VkContext::IndexFormatToVkIndexType(indexFormat));
}

void cmdDraw(const EOS::ICommandBuffer &commandBuffer, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t baseInstance)
{
    CHECK(vertexCount > 0, "Cannot draw with vertex count less then 1");
    if (vertexCount <= 0)
    {
        return;
    }

    const CommandBuffer* vulkanCommandBuffer = dynamic_cast<const CommandBuffer*>(&commandBuffer);
    vkCmdDraw(vulkanCommandBuffer->CommandBufferImpl->VulkanCommandBuffer, vertexCount, instanceCount, firstVertex, baseInstance);
}

void cmdDrawIndexed(const EOS::ICommandBuffer &commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t baseInstance)
{
    CHECK(indexCount > 0, "Cannot draw with index count less then 1");
    if (indexCount <= 0)
    {
        return;
    }

    const CommandBuffer* vulkanCommandBuffer = dynamic_cast<const CommandBuffer*>(&commandBuffer);

    vkCmdDrawIndexed(vulkanCommandBuffer->CommandBufferImpl->VulkanCommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, baseInstance);
}

void cmdPushConstants(const EOS::ICommandBuffer &commandBuffer, const void *data, size_t size, size_t offset)
{
    CHECK(size % 4 == 0, "A push constant must be a multiple of 4");

    const CommandBuffer* vulkanCommandBuffer = dynamic_cast<const CommandBuffer*>(&commandBuffer);
    VulkanContext* vkContext = vulkanCommandBuffer->VkContext;

    CHECK(!vulkanCommandBuffer->CurrentGraphicsPipeline.Empty() || !vulkanCommandBuffer->CurrentComputePipeline.Empty() || !vulkanCommandBuffer->CurrentRayTracingPipeline.Empty(), "No pipeline bound, cannot set pushconstants");


    const VulkanRenderPipelineState* stateGraphics = vkContext->RenderPipelinePool.Get(vulkanCommandBuffer->CurrentGraphicsPipeline);
    //TODO: Add options for compute and ray tracing once these pipelines have been added
    //const VulkanComputePipelineState* stateCompute = vkContext->ComputePipelinesPool.Get(vulkanCommandBuffer->CurrentComputePipeline);
    //const VulkanRayTracingPipelineState* stateRayTracing = vkContext->RayTracingPipelinesPool.Get(vulkanCommandBuffer->CurrentRayTracingPipeline);

   CHECK(stateGraphics, "Graphics State is not valid");


    vkCmdPushConstants(vulkanCommandBuffer->CommandBufferImpl->VulkanCommandBuffer, stateGraphics->PipelineLayout, stateGraphics->ShaderStageFlags, static_cast<uint32_t>(offset), static_cast<uint32_t>(size), data);
}

void cmdPopMarker(const EOS::ICommandBuffer &commandBuffer)
{
    const CommandBuffer* vulkanCommandBuffer = dynamic_cast<const CommandBuffer*>(&commandBuffer);
    vkCmdEndDebugUtilsLabelEXT(vulkanCommandBuffer->CommandBufferImpl->VulkanCommandBuffer);
}

void cmdSetDepthState(const EOS::ICommandBuffer &commandBuffer, const EOS::DepthState &depthState)
{
    const CommandBuffer* vulkanCommandBuffer = dynamic_cast<const CommandBuffer*>(&commandBuffer);

    const VkCompareOp op = VkContext::CompareOpToVkCompareOp(depthState.CompareOpState);
    vkCmdSetDepthWriteEnable(vulkanCommandBuffer->CommandBufferImpl->VulkanCommandBuffer, depthState.IsDepthWriteEnabled ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthTestEnable(vulkanCommandBuffer->CommandBufferImpl->VulkanCommandBuffer, op != VK_COMPARE_OP_ALWAYS || depthState.IsDepthWriteEnabled);
    vkCmdSetDepthCompareOp(vulkanCommandBuffer->CommandBufferImpl->VulkanCommandBuffer, op);
}

void cmdPushMarker(const EOS::ICommandBuffer &commandBuffer, const char *label, uint32_t colorRGBA)
{
    CHECK(label, "You need to have a name set for the marker");

    if (!label)
    {
        return;
    }

    const VkDebugUtilsLabelEXT utilsLabel
    {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pNext = nullptr,
        .pLabelName = label,
        .color
        {
            static_cast<float>((colorRGBA >> 0) & 0xff) / 255.0f,
            static_cast<float>((colorRGBA >> 8) & 0xff) / 255.0f,
            static_cast<float>((colorRGBA >> 16) & 0xff) / 255.0f,
            static_cast<float>((colorRGBA >> 24) & 0xff) / 255.0f
        },
    };

    const CommandBuffer* vulkanCommandBuffer = dynamic_cast<const CommandBuffer*>(&commandBuffer);
    vkCmdBeginDebugUtilsLabelEXT(vulkanCommandBuffer->CommandBufferImpl->VulkanCommandBuffer, &utilsLabel);
}
#pragma endregion


void VulkanBuffer::BufferSubData(const VulkanContext* vulkanContext, size_t offset, size_t size, const void* data)
{
    CHECK(MappedPtr, "buffer is not host-visible");
    if (!MappedPtr)
    {
        return;
    }

    CHECK(offset + size <= BufferSize, "Buffer is not big enough to add the data to it");

    if (data)
    {
        memcpy(static_cast<uint8_t*>(MappedPtr) + offset, data, size);
    }
    else
    {
        memset(static_cast<uint8_t*>(MappedPtr) + offset, 0, size);
    }

    if (!IsCoherentMemory)
    {
        FlushMappedMemory(vulkanContext, offset, size);
    }
}

void VulkanBuffer::FlushMappedMemory(const VulkanContext *vulkanContext, VkDeviceSize offset, VkDeviceSize size) const
{
    CHECK(IsMapped(), "Buffer needs to be mapped to flush its memory");
    if (!IsMapped())
    {
        return;
    }

    vmaFlushAllocation(vulkanContext->vmaAllocator, VMAAllocation, offset, size);
}

VulkanImage::VulkanImage(const ImageDescription &description)
: Image(description.Image)
, UsageFlags(description.UsageFlags)
, Extent(description.Extent)
, ImageType(description.ImageType)
, ImageFormat(description.ImageFormat)
, Levels(description.Levels)
, Layers(description.Layers)
, DebugName(description.DebugName)
{
    VK_ASSERT(VkDebug::SetDebugObjectName(description.Device, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(Image), DebugName));
    CreateImageView(ImageView, description.Device, Image, ImageType, ImageFormat, Levels, Layers, DebugName);
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
    const VkFormat &imageFormat, const uint32_t levels, const uint32_t layers, const char *debugName, const EOS::ComponentMapping& componentMapping)
{
    VkImageAspectFlags aspect{};

    const bool isDepth = IsDepthFormat(imageFormat);
    const bool isStencil = IsStencilFormat(imageFormat);

    if (isDepth || isStencil)
    {
        if (isDepth)
        {
            aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
        }
        else if (isStencil)
        {
            aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }
    else
    {
        aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    const VkComponentMapping mapping
    {
        .r = static_cast<VkComponentSwizzle>(componentMapping.R),
        .g = static_cast<VkComponentSwizzle>(componentMapping.G),
        .b = static_cast<VkComponentSwizzle>(componentMapping.B),
        .a = static_cast<VkComponentSwizzle>(componentMapping.A),
    };

    const VkImageViewCreateInfo createInfo =
   {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = ToImageViewType(imageType),
        .format = imageFormat,
        .components =  mapping,
        .subresourceRange = {aspect, 0, levels , 0, layers},
    };

    VK_ASSERT(vkCreateImageView(device, &createInfo, nullptr, &imageView));
    VK_ASSERT(VkDebug::SetDebugObjectName(device, VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(imageView), debugName));
}

VkImageView VulkanImage::GetImageViewForFramebuffer(VkDevice vulkanDevice, uint32_t level, uint32_t layer)
{
    CHECK(level < EOS_MAX_MIP_LEVELS, "Specified level is bigger then the maximum supported mip levels");
    CHECK(layer < ARRAY_COUNT(ImageViewForFramebuffer[0]), "You can have no more then the set layers");

    if (level >= EOS_MAX_MIP_LEVELS || layer >= ARRAY_COUNT(ImageViewForFramebuffer[0]))
    {
        return VK_NULL_HANDLE;
    }

    if (ImageViewForFramebuffer[level][layer] != VK_NULL_HANDLE)
    {
        return ImageViewForFramebuffer[level][layer];
    }


    if (!DebugName){ DebugName = "ImageView"; }
    const std::string imageViewDebugName = fmt::format("{} - Framebuffer Image View [{}][{}]", DebugName , level, layer);
    CreateImageView(ImageViewForFramebuffer[level][layer] , vulkanDevice, Image, ImageType, ImageFormat, 1, 1, imageViewDebugName.c_str());

    return ImageViewForFramebuffer[level][layer];
}

void VulkanImage::GenerateMipmaps(VkCommandBuffer commandBuffer) const
{
    constexpr uint32_t formatFeatureMask = (VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT);
    const bool hardwareDownscalingSupported = (FormatProperties.optimalTilingFeatures & formatFeatureMask) == formatFeatureMask;
    if (!hardwareDownscalingSupported)
    {
        EOS::Logger->warn("Does not support hardware downscaling while generinting mips for this image:{}", DebugName);
    }


    // Choose linear filter for color formats if supported by the device, else use nearest filter
    // Choose nearest filter by default for depth/stencil formats
    const VkFilter blitFilter = [](bool isDepthOrStencilFormat, bool imageFilterLinear)
    {
        if (isDepthOrStencilFormat) { return VK_FILTER_NEAREST; }
        if (imageFilterLinear) { return VK_FILTER_LINEAR; }
        return VK_FILTER_NEAREST;
    }(VkContext::IsDepthOrStencilFormat(VkContext::vkFormatToFormat(ImageFormat)), FormatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);

    constexpr VkDebugUtilsLabelEXT utilsLabel =
    {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pLabelName = "Generate mipmaps",
        .color = {1.0f, 0.75f, 1.0f, 1.0f},
    };
    vkCmdBeginDebugUtilsLabelEXT(commandBuffer, &utilsLabel);

    // Transition the first level and all layers into VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
    const VkImageAspectFlags imageAspectFlags = GetImageAspectFlags();
    InsertMemoryBarrier(commandBuffer, EOS::Common, EOS::CopySource, {imageAspectFlags, 0, 1, 0, Layers});

    for (uint32_t layer{}; layer < Layers; ++layer)
    {
        int32_t mipWidth = static_cast<int32_t>(Extent.width);
        int32_t mipHeight = static_cast<int32_t>(Extent.height);

        for (uint32_t i{1}; i < Levels; ++i)
        {
            // Transition level i to VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; it will be copied into from the (i-1)-th layer
            InsertMemoryBarrier(commandBuffer, EOS::CopySource, EOS::CopyDest, {imageAspectFlags, i, 1, layer, 1});

            const int32_t nextLevelWidth = mipWidth > 1 ? mipWidth / 2 : 1;
            const int32_t nextLevelHeight = mipHeight > 1 ? mipHeight / 2 : 1;

            const VkOffset3D srcOffsets[2]
            {
                VkOffset3D{0, 0, 0},
                VkOffset3D{mipWidth, mipHeight, 1},
            };

            const VkOffset3D dstOffsets[2]
            {
                VkOffset3D{0, 0, 0},
                VkOffset3D{nextLevelWidth, nextLevelHeight, 1},
            };

            // Blit the image from the prev mip-level (i-1) (VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) to the current mip-level (i) (VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
            const VkImageBlit blit
            {
                .srcSubresource = VkImageSubresourceLayers{imageAspectFlags, i - 1, layer, 1},
                .srcOffsets = {srcOffsets[0], srcOffsets[1]},
                .dstSubresource = VkImageSubresourceLayers{imageAspectFlags, i, layer, 1},
                .dstOffsets = {dstOffsets[0], dstOffsets[1]},
            };

            vkCmdBlitImage(commandBuffer, Image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, blitFilter);

            // Transition i-th level to VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL as it will be read from in the next iteration
            InsertMemoryBarrier(commandBuffer, EOS::CopyDest, EOS::CopySource, {imageAspectFlags, i, 1, layer, 1});

            // Compute the size of the next mip level
            mipWidth = nextLevelWidth;
            mipHeight = nextLevelHeight;
        }
    }

    // Transition all levels and layers (faces) to their final layout
    InsertMemoryBarrier(commandBuffer, EOS::CopySource, EOS::Undefined, {imageAspectFlags, 0, Levels, 0, Layers});
    vkCmdEndDebugUtilsLabelEXT(commandBuffer);
}

VkImageAspectFlags VulkanImage::GetImageAspectFlags() const
{
    //TODO Move this functionaliyt towards  VkSynchronization::ConvertToVkImageAspectFlags or remove that one and use this one. which may be even better.
    const bool isDepthFormat =  IsDepthFormat(ImageFormat);
    const bool isStencilFormat =  IsStencilFormat(ImageFormat);

    VkImageAspectFlags flags = 0;
    flags |= isDepthFormat ? VK_IMAGE_ASPECT_DEPTH_BIT : 0;
    flags |= isStencilFormat ? VK_IMAGE_ASPECT_STENCIL_BIT : 0;
    flags |= !(isDepthFormat || isStencilFormat) ? VK_IMAGE_ASPECT_COLOR_BIT : 0;

    return flags;
}

void VulkanImage::InsertMemoryBarrier(VkCommandBuffer commandBuffer, EOS::ResourceState currentState, EOS::ResourceState nextState, const VkImageSubresourceRange& subResourceRange) const
{
    VkImageMemoryBarrier2 vkBarrier
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR,
        .pNext = nullptr,
        .srcStageMask       = VkSynchronization::ConvertToVkPipelineStage2(currentState),
        .srcAccessMask      = VkSynchronization::ConvertToVkAccessFlags2(currentState),
        .dstStageMask       = VkSynchronization::ConvertToVkPipelineStage2(nextState),
        .dstAccessMask      = VkSynchronization::ConvertToVkAccessFlags2(nextState),
        .oldLayout          = VkSynchronization::ConvertToVkImageLayout(currentState),
        .newLayout          = VkSynchronization::ConvertToVkImageLayout(nextState),
        .image              = Image,
        .subresourceRange   = subResourceRange
    };

    const VkDependencyInfoKHR dependencyInfo
    {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
        .pNext = nullptr,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &vkBarrier
    };

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
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
            CHECK(result == VK_SUCCESS, "Failed Waiting for Fences");
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

VulkanPipelineBuilder::VulkanPipelineBuilder()
    //TODO: set theses states again after .Build() and make static
    : VertexInputStateInfo(VkPipelineVertexInputStateCreateInfo
          {
              .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
              .vertexBindingDescriptionCount = 0,
              .pVertexBindingDescriptions = nullptr,
              .vertexAttributeDescriptionCount = 0,
              .pVertexAttributeDescriptions = nullptr,
          })
    , InputAssemblyState(
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .flags = 0,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitiveRestartEnable = VK_FALSE,
        })
    , RasterizationState(
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .flags = 0,
            .depthClampEnable = VK_FALSE,
            .rasterizerDiscardEnable = VK_FALSE,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depthBiasEnable = VK_FALSE,
            .depthBiasConstantFactor = 0.0f,
            .depthBiasClamp = 0.0f,
            .depthBiasSlopeFactor = 0.0f,
            .lineWidth = 1.0f,
        })
    , MultisampleState(
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
            .sampleShadingEnable = VK_FALSE,
            .minSampleShading = 0.0f,
            .pSampleMask = nullptr,
            .alphaToCoverageEnable = VK_FALSE,
            .alphaToOneEnable = VK_FALSE,
        })
    , DepthStencilState(
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .pNext = VK_NULL_HANDLE,
            .flags = 0,
            .depthTestEnable = VK_FALSE,
            .depthWriteEnable = VK_FALSE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
            .depthBoundsTestEnable = VK_FALSE,
            .stencilTestEnable = VK_FALSE,
            .front =
            {
                .failOp = VK_STENCIL_OP_KEEP,
                .passOp = VK_STENCIL_OP_KEEP,
                .depthFailOp = VK_STENCIL_OP_KEEP,
                .compareOp = VK_COMPARE_OP_NEVER,
                .compareMask = 0,
                .writeMask = 0,
                .reference = 0,
            },
            .back =
            {
                .failOp = VK_STENCIL_OP_KEEP,
                .passOp = VK_STENCIL_OP_KEEP,
                .depthFailOp = VK_STENCIL_OP_KEEP,
                .compareOp = VK_COMPARE_OP_NEVER,
                .compareMask = 0,
                .writeMask = 0,
                .reference = 0,
            },
            .minDepthBounds = 0.0f,
            .maxDepthBounds = 1.0f,
        })
    , TesselationState(
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,
            .flags = 0,
            .patchControlPoints = 0,
        }){}

VulkanPipelineBuilder& VulkanPipelineBuilder::DynamicState(const VkDynamicState state)
{
    CHECK(NumberOfDynamicStates < MaxDynamicStates, "You can have no more then MaxDynamicStates in a single VulkanPipeline");
    DynamicStates[NumberOfDynamicStates++] = state;

    return *this;
}

VulkanPipelineBuilder& VulkanPipelineBuilder::PrimitiveTypology(const VkPrimitiveTopology topology)
{
    InputAssemblyState.topology = topology;
    return *this;
}

VulkanPipelineBuilder& VulkanPipelineBuilder::RasterizationSamples(const VkSampleCountFlagBits samples, const float minSampleShading)
{
    MultisampleState.rasterizationSamples = samples;
    MultisampleState.sampleShadingEnable = minSampleShading > 0 ? VK_TRUE : VK_FALSE;
    MultisampleState.minSampleShading = minSampleShading;

    return *this;
}

VulkanPipelineBuilder& VulkanPipelineBuilder::ShaderStage(const VkPipelineShaderStageCreateInfo &stage)
{
    if (stage.module != VK_NULL_HANDLE)
    {
        CHECK(NumberOfShaderStages < ARRAY_COUNT(ShaderStages), "You can have no more of Max Shader Stages Count of shader stages");
        ShaderStages[NumberOfShaderStages++] = stage;
    }

    return *this;
}

VulkanPipelineBuilder& VulkanPipelineBuilder::CullMode(const VkCullModeFlags mode)
{
    RasterizationState.cullMode = mode;
    return *this;
}

VulkanPipelineBuilder & VulkanPipelineBuilder::StencilStateOps(const VkStencilFaceFlags faceMask, const VkStencilOp failOp, const VkStencilOp passOp, const VkStencilOp depthFailOp, const VkCompareOp compareOp)
{
    DepthStencilState.stencilTestEnable = DepthStencilState.stencilTestEnable == VK_TRUE || failOp != VK_STENCIL_OP_KEEP || passOp != VK_STENCIL_OP_KEEP || depthFailOp != VK_STENCIL_OP_KEEP || compareOp != VK_COMPARE_OP_ALWAYS
                                             ? VK_TRUE
                                             : VK_FALSE;

    if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
    {
        VkStencilOpState& s = DepthStencilState.front;
        s.failOp = failOp;
        s.passOp = passOp;
        s.depthFailOp = depthFailOp;
        s.compareOp = compareOp;
    }

    if (faceMask & VK_STENCIL_FACE_BACK_BIT)
    {
        VkStencilOpState& s = DepthStencilState.back;
        s.failOp = failOp;
        s.passOp = passOp;
        s.depthFailOp = depthFailOp;
        s.compareOp = compareOp;
    }

    return *this;
}

VulkanPipelineBuilder& VulkanPipelineBuilder::StencilMasks(VkStencilFaceFlags faceMask, uint32_t compareMask, uint32_t writeMask, uint32_t reference)
{
    if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
    {
        VkStencilOpState& s = DepthStencilState.front;
        s.compareMask = compareMask;
        s.writeMask = writeMask;
        s.reference = reference;
    }

    if (faceMask & VK_STENCIL_FACE_BACK_BIT)
    {
        VkStencilOpState& s = DepthStencilState.back;
        s.compareMask = compareMask;
        s.writeMask = writeMask;
        s.reference = reference;
    }

    return *this;
}

VulkanPipelineBuilder & VulkanPipelineBuilder::FrontFace(const VkFrontFace mode)
{
    RasterizationState.frontFace = mode;
    return *this;
}

VulkanPipelineBuilder & VulkanPipelineBuilder::PolygonMode(const VkPolygonMode mode)
{
    RasterizationState.polygonMode = mode;
    return *this;
}

VulkanPipelineBuilder & VulkanPipelineBuilder::VertexInputState(const VkPipelineVertexInputStateCreateInfo& state)
{
    VertexInputStateInfo = state;
    return *this;
}

VulkanPipelineBuilder & VulkanPipelineBuilder::ColorAttachments(const VkPipelineColorBlendAttachmentState* states,const VkFormat* formats, uint32_t numColorAttachments)
{
    CHECK(states, "States are not valid");
    CHECK(formats, "Formats are not valid");
    CHECK(numColorAttachments <= ARRAY_COUNT(ColorBlendAttachmentStates), "You can have no more then {} ColorAttachments", ARRAY_COUNT(ColorBlendAttachmentStates));
    CHECK(numColorAttachments <= ARRAY_COUNT(ColorAttachmentFormats), "You can have no more then {} ColorAttachments", ARRAY_COUNT(ColorBlendAttachmentStates));

    for (uint32_t i{}; i != numColorAttachments; ++i)
    {
        ColorBlendAttachmentStates[i] = states[i];
        ColorAttachmentFormats[i] = formats[i];
    }
    NumberOfColorAttachments = numColorAttachments;

    return *this;
}

VulkanPipelineBuilder & VulkanPipelineBuilder::DepthAttachmentFormat(VkFormat format)
{
    DepthAttachmentFormatInfo = format;
    return *this;
}

VulkanPipelineBuilder & VulkanPipelineBuilder::StencilAttachmentFormat(VkFormat format)
{
    StencilAttachmentFormatInfo = format;
    return *this;
}

VulkanPipelineBuilder & VulkanPipelineBuilder::PatchControlPoints(uint32_t numPoints)
{
    TesselationState.patchControlPoints = numPoints;
    return *this;
}

VkResult VulkanPipelineBuilder::Build(VkDevice device, VkPipelineCache pipelineCache, VkPipelineLayout pipelineLayout, VkPipeline*outPipeline, const char *debugName) noexcept
{
    const VkPipelineDynamicStateCreateInfo dynamicState
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = NumberOfDynamicStates,
        .pDynamicStates = DynamicStates,
    };

    // viewport and scissor can be NULL if the viewport state is dynamic
    constexpr VkPipelineViewportStateCreateInfo viewportState
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = nullptr,
        .scissorCount = 1,
        .pScissors = nullptr,
    };

    const VkPipelineColorBlendStateCreateInfo colorBlendState
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = NumberOfColorAttachments,
        .pAttachments = ColorBlendAttachmentStates,
    };

    const VkPipelineRenderingCreateInfo renderingInfo
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .pNext = nullptr,
        .colorAttachmentCount = NumberOfColorAttachments,
        .pColorAttachmentFormats = ColorAttachmentFormats,
        .depthAttachmentFormat = DepthAttachmentFormatInfo,
        .stencilAttachmentFormat = StencilAttachmentFormatInfo,
    };

    const VkGraphicsPipelineCreateInfo ci
    {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .flags = 0,
        .stageCount = NumberOfShaderStages,
        .pStages = ShaderStages,
        .pVertexInputState = &VertexInputStateInfo,
        .pInputAssemblyState = &InputAssemblyState,
        .pTessellationState = &TesselationState,
        .pViewportState = &viewportState,
        .pRasterizationState = &RasterizationState,
        .pMultisampleState = &MultisampleState,
        .pDepthStencilState = &DepthStencilState,
        .pColorBlendState = &colorBlendState,
        .pDynamicState = &dynamicState,
        .layout = pipelineLayout,
        .renderPass = VK_NULL_HANDLE,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };

    const VkResult result = vkCreateGraphicsPipelines(device, nullptr, 1, &ci, nullptr, outPipeline);
    CHECK(result == VK_SUCCESS, "Could Not Create Graphics Pipeline");

    if (result != VK_SUCCESS)
    {
        return result;
    }

    ++NumberOfCreatedPipelines;
    return VkDebug::SetDebugObjectName(device, VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(*outPipeline), debugName);
}

VulkanStagingDevice::VulkanStagingDevice(VulkanContext* context)
: VkContext(context)
, StagingBuffer(context, EOS::BufferHandle())
{
}

void VulkanStagingDevice::EnsureSize(uint32_t sizeNeeded)
{
    const uint32_t alignedSize = std::max(EOS::GetSizeAligned(sizeNeeded, Alignment), MinBufferSize);
    sizeNeeded = alignedSize < MaxBufferSize ? alignedSize : MaxBufferSize;

    if (!StagingBuffer.Empty())
    {
        const bool isEnoughSize = sizeNeeded <= Size;
        const bool isMaxSize = Size == MaxBufferSize;

        if (isEnoughSize || isMaxSize)
        {
            return;
        }
    }

    WaitAndReset();

    // deallocate the previous staging buffer
    StagingBuffer = nullptr;

    // if the combined size of the new staging buffer and the existing one is larger than the limit imposed by some architectures on buffers
    // that are device and host visible, we need to wait for the current buffer to be destroyed before we can allocate a new one
    if ((sizeNeeded + Size) > MaxBufferSize)
    {
        VkContext->WaitOnDeferredTasks();
    }

    Size = sizeNeeded;

    const std::string debugName = fmt::format("Buffer: staging buffer {}", ++Counter);

    EOS::BufferHandle bufferHandle = VkContext->CreateBuffer(Size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, debugName.c_str());
    StagingBuffer = {VkContext, bufferHandle };

    CHECK(!StagingBuffer.Empty(), "Could not create StagingBuffer");
}

void VulkanStagingDevice::WaitAndReset()
{
    for (const MemoryRegionDescription& region : Regions)
    {
        VkContext->VulkanCommandPool->Wait(region.Handle);
    };

    Regions.clear();
    Regions.push_front({0, Size, EOS::SubmitHandle()});
}

void VulkanStagingDevice::BufferSubData(const EOS::Handle<EOS::Buffer> &buffer, size_t dstOffset, size_t size, const void *data)
{
    VulkanBuffer* vulkanBuffer = VkContext->BufferPool.Get(buffer);
    CHECK(vulkanBuffer, "The buffer from the handle is not valid");
    CHECK(dstOffset + size <= vulkanBuffer->BufferSize, "The data you want to upload is too big for the buffer");

    if (vulkanBuffer->IsMapped())
    {
        vulkanBuffer->BufferSubData(VkContext, dstOffset, size, data);
        return;
    }

    VulkanBuffer* stagingBuffer = VkContext->BufferPool.Get(StagingBuffer);
    CHECK(stagingBuffer, "Staging buffer is not valid");

    while (size)
    {
        // get next staging buffer free offset
        MemoryRegionDescription desc = GetNextFreeOffset(static_cast<uint32_t>(size));
        const uint32_t chunkSize = std::min(static_cast<uint32_t>(size), static_cast<uint32_t>(desc.Size));

        // copy data into staging buffer
        stagingBuffer->BufferSubData(VkContext, desc.Offset, chunkSize, data);

        // do the transfer
        const VkBufferCopy copy =
        {
            .srcOffset = desc.Offset,
            .dstOffset = dstOffset,
            .size = chunkSize,
        };

        CommandBufferData* commandbuffer = VkContext->VulkanCommandPool->AcquireCommandBuffer();

        vkCmdCopyBuffer(commandbuffer->VulkanCommandBuffer, stagingBuffer->VulkanVkBuffer, vulkanBuffer->VulkanVkBuffer, 1, &copy);

        //TODO: Check states
        EOS::GlobalBarrier globBarrier{buffer,  EOS::CopySource,EOS::Common };
        VkMemoryBarrier2 vkBarrier
        {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2_KHR,
            .pNext = nullptr,
            .srcStageMask   = VkSynchronization::ConvertToVkPipelineStage2(globBarrier.CurrentState),
            .srcAccessMask  = VkSynchronization::ConvertToVkAccessFlags2(globBarrier.CurrentState),
            .dstStageMask   = VkSynchronization::ConvertToVkPipelineStage2(globBarrier.NextState),
            .dstAccessMask  = VkSynchronization::ConvertToVkAccessFlags2(globBarrier.NextState)
        };

        const VkDependencyInfoKHR dependencyInfo
        {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 1u,
            .pMemoryBarriers = &vkBarrier,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 0,
            .pImageMemoryBarriers = nullptr
        };

        vkCmdPipelineBarrier2(commandbuffer->VulkanCommandBuffer, &dependencyInfo);

        desc.Handle = VkContext->VulkanCommandPool->Submit(*commandbuffer);
        Regions.push_back(desc);

        size -= chunkSize;
        data = chunkSize + static_cast<const uint8_t*>(data);
        dstOffset += chunkSize;
    }
}

void VulkanStagingDevice::ImageData2D(VulkanImage &image, const VkRect2D &imageRegion, uint32_t baseMipLevel, uint32_t numMipLevels, uint32_t layer, uint32_t numLayers, VkFormat format, const void *data)
{
    CHECK(numMipLevels <= EOS_MAX_MIP_LEVELS, "mip levels exceed max mip levels");

    // divide the width and height by 2 until we get to the size of level 'baseMipLevel'
    uint32_t width = image.Extent.width >> baseMipLevel;
    uint32_t height = image.Extent.height >> baseMipLevel;

    const EOS::Format texFormat(VkContext::vkFormatToFormat(format));
    CHECK(!imageRegion.offset.x && !imageRegion.offset.y && imageRegion.extent.width == width && imageRegion.extent.height == height, "Uploading mip-levels with an image region that is smaller than the base mip level is not supported");

    // find the storage size for all mip-levels being uploaded
    uint32_t layerStorageSize = 0;
    for (uint32_t i = 0; i < numMipLevels; ++i)
    {
        const uint32_t mipSize = VkContext::GetTextureBytesPerLayer(image.Extent.width, image.Extent.height, texFormat, i);
        layerStorageSize += mipSize;
        width = width <= 1 ? 1 : width >> 1;
        height = height <= 1 ? 1 : height >> 1;
    }

    const uint32_t storageSize = layerStorageSize * numLayers;
    EnsureSize(storageSize);
    CHECK(storageSize <= Size, "storageSize exceeds available size for the staging device");

    MemoryRegionDescription desc = GetNextFreeOffset(storageSize);

    // No support for copying image in multiple smaller chunk sizes. If we get smaller buffer size than storageSize, we will wait for GPU idle
    // and get bigger chunk.
    if (desc.Size < storageSize)
    {
        WaitAndReset();
        desc = GetNextFreeOffset(storageSize);
    }
    CHECK(desc.Size >= storageSize, "the needed size is bigger then the storageSize");

    CommandBufferData* wrapper = VkContext->VulkanCommandPool->AcquireCommandBuffer();
    CHECK(wrapper, "The Aquired CommandBuffer is not valid.");

    VulkanBuffer* stagingBuffer = VkContext->BufferPool.Get(StagingBuffer);
    CHECK(stagingBuffer, "The staging buffer handle does not hold a valid staging buffer");
    stagingBuffer->BufferSubData(VkContext, desc.Offset, storageSize, data);

    uint32_t offset = 0;
    const uint32_t numPlanes = VkContext::GetNumberOfImagePlanes(image.ImageFormat);
    if (numPlanes > 1)
    {
        assert(false); //Multiple planes arre not supported yet.
        CHECK(layer == 0 && baseMipLevel == 0, "Your image layers and levels should be the same with multiple image planes.");
        CHECK(numLayers == 1 && numMipLevels == 1, "Your image layers and levels should be the same with multiple image planes.");
        CHECK(imageRegion.offset.x == 0 && imageRegion.offset.y == 0, "You can't have a image offset with multiple image planes.");
        CHECK(image.ImageType == EOS::ImageType::Image_2D, "Your image must be a 2D image with multiple image planes");
        CHECK(image.Extent.width == imageRegion.extent.width && image.Extent.height == imageRegion.extent.height, "The image extent is not correct");
    }

    VkImageAspectFlags imageAspect = VK_IMAGE_ASPECT_COLOR_BIT;
    if (numPlanes == 2) { imageAspect = VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT; }
    if (numPlanes == 3) { imageAspect = VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT | VK_IMAGE_ASPECT_PLANE_2_BIT; }

    // https://registry.khronos.org/KTX/specs/1.0/ktxspec.v1.html
    for (uint32_t mipLevel{}; mipLevel < numMipLevels; ++mipLevel)
    {
        for (constexpr uint32_t currentLayer{}; layer != numLayers; layer++)
        {
            const uint32_t currentMipLevel = baseMipLevel + mipLevel;
            CHECK(currentMipLevel < image.Levels, "");
            CHECK(mipLevel < image.Levels, "");

            // 1. Transition initial image layout into TRANSFER_DST_OPTIMAL
            image.InsertMemoryBarrier(wrapper->VulkanCommandBuffer, EOS::Undefined, EOS::CopyDest, {imageAspect, currentMipLevel, 1, currentLayer, 1});

            // 2. Copy the pixel data from the staging buffer into the image
            uint32_t planeOffset = 0;
            for (uint32_t plane{}; plane != numPlanes; ++plane)
            {
                //TODO: These extent should take YUV images into account once they are supported.
                const VkExtent2D extent
                {
                    .width = std::max(1u, imageRegion.extent.width >> mipLevel),
                    .height = std::max(1u, imageRegion.extent.height >> mipLevel),
                };

                const VkRect2D region
                {
                    .offset = {.x = imageRegion.offset.x >> mipLevel, .y = imageRegion.offset.y >> mipLevel},
                    .extent = extent,
                };

                const VkBufferImageCopy copy
                {
                    // the offset for this level is at the start of all mip-levels plus the size of all previous mip-levels being uploaded
                    .bufferOffset = desc.Offset + offset + planeOffset,
                    .bufferRowLength = 0,
                    .bufferImageHeight = 0,
                    .imageSubresource =
                    VkImageSubresourceLayers{numPlanes > 1 ? VK_IMAGE_ASPECT_PLANE_0_BIT << plane : imageAspect, currentMipLevel, layer, 1},
                    .imageOffset = {.x = region.offset.x, .y = region.offset.y, .z = 0},
                    .imageExtent = {.width = region.extent.width, .height = region.extent.height, .depth = 1u},
                };

                vkCmdCopyBufferToImage(wrapper->VulkanCommandBuffer, stagingBuffer->VulkanVkBuffer, image.Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                planeOffset += VkContext::GetTextureBytesPerLayer(imageRegion.extent.width, imageRegion.extent.height, texFormat, plane); //TODO Should also take into acount of YUV images which are not supported yet.
            }

            // 3. Transition TRANSFER_DST_OPTIMAL into SHADER_READ_ONLY_OPTIMAL
            image.InsertMemoryBarrier(wrapper->VulkanCommandBuffer, EOS::CopyDest, EOS::ShaderResource, VkImageSubresourceRange{imageAspect, currentMipLevel, 1, layer, 1});
            offset += VkContext::GetTextureBytesPerLayer(imageRegion.extent.width, imageRegion.extent.height, texFormat, currentMipLevel);
        }
    }

    //TODO: Doesn't make sense that aquire returns a pointer and submit requires a reference
    desc.Handle = VkContext->VulkanCommandPool->Submit(*wrapper);
    Regions.emplace_back(desc);
}

VulkanStagingDevice::MemoryRegionDescription VulkanStagingDevice::GetNextFreeOffset(uint32_t size)
{
    const uint32_t requestedAlignedSize = EOS::GetSizeAligned(size, Alignment);

    EnsureSize(requestedAlignedSize);
    CHECK(!Regions.empty(), "Memory Regions are empty");

    for (auto it = Regions.begin(); it != Regions.end(); ++it)
    {
        // Check if region is available
        if (VkContext->VulkanCommandPool->IsReady(it->Handle))
        {
            //Check if region is big enoug
            if (it->Size >= requestedAlignedSize)
            {
                const uint32_t unusedSize = it->Size - requestedAlignedSize;
                const uint32_t unusedOffset = it->Offset + requestedAlignedSize;

                //Replace the region with the smaller one.
                Regions.erase(it);
                if (unusedSize > 0)
                {
                    Regions.push_front({unusedOffset, unusedSize, EOS::SubmitHandle()});
                }

                return {it->Offset, requestedAlignedSize, EOS::SubmitHandle()}; // New Region that we will use
            }
        }
    }

    //Wait for the staging buffer to become free.
    WaitAndReset();
    Regions.clear();

    const uint32_t unusedSize = Size > requestedAlignedSize ? Size - requestedAlignedSize : 0;
    if (unusedSize)
    {
        const uint32_t unusedOffset = Size - unusedSize;
        Regions.push_front({unusedOffset, unusedSize, EOS::SubmitHandle()});
    }

    return
    {
        .Offset = 0,
        .Size = Size - unusedSize,
        .Handle = EOS::SubmitHandle(),
    };
}

VulkanContext::VulkanContext(const EOS::ContextCreationDescription& contextDescription)
: Configuration(contextDescription.Config)
{
    CHECK(volkInitialize() == VK_SUCCESS, "Failed to Initialize VOLK");

    CreateVulkanInstance(contextDescription.ApplicationName);
    SetupDebugMessenger();
    CreateSurface(contextDescription.Window, contextDescription.Display);

    //Select the Physical Device
    std::vector<EOS::HardwareDeviceDescription> hardwareDevices;
    GetHardwareDevice(contextDescription.PreferredHardwareType, hardwareDevices);
    VkContext::SelectHardwareDevice(hardwareDevices, VulkanPhysicalDevice);

    //Create our Vulkan Device
    VkContext::CreateVulkanDevice(VulkanDevice, VulkanPhysicalDevice, VulkanDeviceQueues);

    //Create SwapChain
    VulkanSwapChainCreationDescription desc
    {
        .vulkanContext = this,
        .width = static_cast<uint32_t>(contextDescription.Width),
        .height = static_cast<uint32_t>(contextDescription.Height),
    };

    SwapChain = std::make_unique<VulkanSwapChain>(desc);

    //Create our Timeline Semaphore
    TimelineSemaphore = VkSynchronization::CreateSemaphoreTimeline(VulkanDevice, SwapChain->GetNumSwapChainImages() - 1, "Semaphore: TimelineSemaphore");

    //Create our CommandPool
    VulkanCommandPool = std::make_unique<CommandPool>(VulkanDevice, VulkanDeviceQueues.Graphics.QueueFamilyIndex);

    //TODO: Create pipeline cache

    UseStagingDevice = IsHostVisibleMemorySingleHeap();

    CreateAllocator();

    VulkanStagingBuffer = std::make_unique<VulkanStagingDevice>(this);

    GrowDescriptorPool(16, 16, 1);
}

VulkanContext::~VulkanContext()
{
    //Wait unit all work has been done
    VK_ASSERT(vkDeviceWaitIdle(VulkanDevice));

    SwapChain.reset(nullptr);
    VulkanStagingBuffer.reset(nullptr);

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

    if (RenderPipelinePool.NumObjects())
    {
        EOS::Logger->error("{} Leaked Render Pipelines", RenderPipelinePool.NumObjects());
    }
    RenderPipelinePool.Clear();

    if (BufferPool.NumObjects())
    {
        EOS::Logger->error("{} Leaked Buffers", BufferPool.NumObjects());
    }
    BufferPool.Clear();

    WaitOnDeferredTasks();

    VulkanCommandPool.reset(nullptr);

    vkDestroySurfaceKHR(VulkanInstance, VulkanSurface, nullptr);
    vkDestroyDescriptorSetLayout(VulkanDevice, DescriptorSetLayout, nullptr);
    vkDestroyDescriptorPool(VulkanDevice, DescriptorPool, nullptr);
    vmaDestroyAllocator(vmaAllocator);

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

EOS::Format VulkanContext::GetSwapchainFormat() const
{
    if (!HasSwapChain())
    {
        EOS::Logger->error("Context: {} does not have a valid swapchain", reinterpret_cast<uint64_t>(this));
        return EOS::Format::Invalid;
    }

    return VkContext::vkFormatToFormat(SwapChain->GetFormat().format);
}

EOS::Holder<EOS::ShaderModuleHandle> VulkanContext::CreateShaderModule(const EOS::ShaderInfo &shaderInfo)
{
    VkShaderModule vkShaderModule = VK_NULL_HANDLE;

    const VkShaderModuleCreateInfo createInfo =
    {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = shaderInfo.Spirv.size() * sizeof(uint32_t),
        .pCode = shaderInfo.Spirv.data(),
    };

    VK_ASSERT(vkCreateShaderModule(VulkanDevice, &createInfo, nullptr, &vkShaderModule);)
    CHECK(vkShaderModule != VK_NULL_HANDLE, "Failed to create shader module from ShaderInfo");

    VK_ASSERT(VkDebug::SetDebugObjectName(VulkanDevice, VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<uint64_t>(vkShaderModule), shaderInfo.DebugName));

    VulkanShaderModuleState state
    {
        .ShaderModule = vkShaderModule,
        .PushConstantsSize = shaderInfo.PushConstantSize
    };

    return {this, ShaderModulePool.Create(std::move(state))};
}

EOS::Holder<EOS::RenderPipelineHandle> VulkanContext::CreateRenderPipeline(const EOS::RenderPipelineDescription& renderPipelineDescription)
{
    //Check Attachments
    const bool hasColorAttachments = renderPipelineDescription.GetNumColorAttachments() > 0;
    const bool hasDepthAttachment = renderPipelineDescription.DepthFormat != EOS::Format::Invalid;
    const bool hasAnyAttachments = hasColorAttachments || hasDepthAttachment;

    CHECK(hasAnyAttachments, "At least one attachment is needed in a render pipeline");
    if (!hasAnyAttachments) { return {};  }

    //Check Tesselation Setup
    if (renderPipelineDescription.TessellationControlShader.Valid() || renderPipelineDescription.TesselationShader.Valid() || renderPipelineDescription.PatchControlPoints)
    {
        const bool isTesselationOkay = renderPipelineDescription.TessellationControlShader.Valid() && renderPipelineDescription.TesselationShader.Valid();
        CHECK(isTesselationOkay, "You need both Tesselation Control and Evaluation Shaders");

        if (!isTesselationOkay){return {}; }

        //Device Properties
        VkPhysicalDeviceProperties2 vkPhysicalDeviceProperties2;
        VkPhysicalDeviceDriverProperties vkPhysicalDeviceDriverProperties;
        uint32_t SDKApiVersion{};
        vkEnumerateInstanceVersion(&SDKApiVersion);
        const uint32_t SDKMinor = VK_API_VERSION_MINOR(SDKApiVersion);
        VkContext::GetPhysicalDeviceProperties(vkPhysicalDeviceProperties2, vkPhysicalDeviceDriverProperties, VulkanPhysicalDevice, SDKMinor);

        const bool areControlPointsOkay = renderPipelineDescription.PatchControlPoints > 0 && renderPipelineDescription.PatchControlPoints < vkPhysicalDeviceProperties2.properties.limits.maxTessellationPatchSize;
        CHECK(areControlPointsOkay, "You can have no more tesselation control points then: {} or less then: 1", vkPhysicalDeviceProperties2.properties.limits.maxTessellationPatchSize);
        if (!areControlPointsOkay){ return {}; }
    }

    const bool hasVertexShader = renderPipelineDescription.VertexShader.Valid();

    //Check Mesh Shader Setup
    if (renderPipelineDescription.MeshShader.Valid())
    {
        CHECK(!hasVertexShader, "Cannot have vertex shader with mesh shaders");
        if (hasVertexShader) {return {}; }

        const bool hasAttributesOrInputBindings = renderPipelineDescription.VertexInput.GetNumAttributes() == 0 && renderPipelineDescription.VertexInput.GetNumInputBindings() == 0;
        CHECK(!hasAttributesOrInputBindings, "Cannot have vertexInput with mesh shaders");
        if (hasAttributesOrInputBindings) {return {}; }

        const bool hasTesselationShader = renderPipelineDescription.TesselationShader.Valid() || renderPipelineDescription.TessellationControlShader.Valid();
        CHECK(!hasTesselationShader, "Cannot have tesselation shader with mesh shaders");
        if (hasTesselationShader) {return {}; }

        const bool hasGeometryShader = renderPipelineDescription.GeometryShader.Valid();
        CHECK(!hasGeometryShader, "Cannot have geometry shader with mesh shaders");
        if (hasGeometryShader){return {}; }
    }
    else
    {
        CHECK(hasVertexShader, "You need a vertex shader for a renderpipeline that doesn't use a mesh shader");
        if (!hasVertexShader) {return {}; }
    }

    const bool hasFragmentShader = renderPipelineDescription.FragmentShader.Valid();
    CHECK(hasFragmentShader, "Missing fragment shader");
    if (!hasFragmentShader){return {}; }


    VulkanRenderPipelineState renderPipelineState = {.Description = renderPipelineDescription};

    // Iterate and cache vertex input bindings and attributes
    const EOS::VertexInputData& vertexInput = renderPipelineState.Description.VertexInput;

    bool bufferAlreadyBound[EOS::VertexInputData::MAX_BUFFERS]{};

    renderPipelineState.NumberOfAttributes = vertexInput.GetNumAttributes();

    for (uint32_t i{}; i != renderPipelineState.NumberOfAttributes; ++i)
    {
        const EOS::VertexInputData::VertexAttribute& attr = vertexInput.Attributes[i];

        renderPipelineState.Attributes[i] =
        {
            .location = attr.Location,
            .binding = attr.Binding,
            .format = VkContext::VertexFormatToVkFormat(attr.Format),
            .offset = static_cast<uint32_t>(attr.Offset)
        };

        if (!bufferAlreadyBound[attr.Binding])
        {
            bufferAlreadyBound[attr.Binding] = true;
            renderPipelineState.Bindings[renderPipelineState.NumberOfBindings++] =
            {
                .binding = attr.Binding,
                .stride = vertexInput.InputBindings[attr.Binding].Stride,
                .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
            };
        }
    }

    if (renderPipelineDescription.SpecInfo.Data && renderPipelineDescription.SpecInfo.DataSize)
    {
        // copy into a local storage
        renderPipelineState.SpecConstantDataStorage = malloc(renderPipelineDescription.SpecInfo.DataSize);
        memcpy(renderPipelineState.SpecConstantDataStorage, renderPipelineDescription.SpecInfo.Data, renderPipelineDescription.SpecInfo.DataSize);
        renderPipelineState.Description.SpecInfo.Data = renderPipelineState.SpecConstantDataStorage;
    }

    //Create the vulkan pipeline and pipeline layout
    // build a new Vulkan pipeline
    const EOS::RenderPipelineDescription& description = renderPipelineState.Description;
    const uint32_t numColorAttachments = description.GetNumColorAttachments();


    // Not all attachments are valid. We need to create color blend attachments only for active attachments
    VkPipelineColorBlendAttachmentState colorBlendAttachmentStates[EOS_MAX_COLOR_ATTACHMENTS]{};
    VkFormat colorAttachmentFormats[EOS_MAX_COLOR_ATTACHMENTS]{};

    for (uint32_t i{}; i != numColorAttachments; ++i)
    {
        const EOS::ColorAttachment& attachment = description.ColorAttachments[i];
        CHECK(attachment.ColorFormat != EOS::Format::Invalid, "The Color Attachment Format is invalid.");

        colorAttachmentFormats[i] = VkContext::FormatTovkFormat(attachment.ColorFormat);
        if (!attachment.BlendEnabled)
        {
            colorBlendAttachmentStates[i] = VkPipelineColorBlendAttachmentState
            {
                .blendEnable = VK_FALSE,
                .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
                .colorBlendOp = VK_BLEND_OP_ADD,
                .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
                .alphaBlendOp = VK_BLEND_OP_ADD,
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            };
        }
        else
        {
            colorBlendAttachmentStates[i] = VkPipelineColorBlendAttachmentState
            {
                .blendEnable = VK_TRUE,
                .srcColorBlendFactor = VkContext::BlendFactorToVkBlendFactor(attachment.SrcRGBBlendFactor),
                .dstColorBlendFactor = VkContext::BlendFactorToVkBlendFactor(attachment.DstRGBBlendFactor),
                .colorBlendOp = VkContext::BlendOpToVkBlendOp(attachment.RGBBlendOp),
                .srcAlphaBlendFactor = VkContext::BlendFactorToVkBlendFactor(attachment.SrcAlphaBlendFactor),
                .dstAlphaBlendFactor = VkContext::BlendFactorToVkBlendFactor(attachment.DstAlphaBlendFactor),
                .alphaBlendOp = VkContext::BlendOpToVkBlendOp(attachment.AlphaBlendOp),
                .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
            };
        }
    }

    const VulkanShaderModuleState* vertModule = ShaderModulePool.Get(description.VertexShader);
    const VulkanShaderModuleState* tescModule = ShaderModulePool.Get(description.TessellationControlShader);
    const VulkanShaderModuleState* teseModule = ShaderModulePool.Get(description.TesselationShader);
    const VulkanShaderModuleState* geomModule = ShaderModulePool.Get(description.GeometryShader);
    const VulkanShaderModuleState* fragModule = ShaderModulePool.Get(description.FragmentShader);
    const VulkanShaderModuleState* taskModule = ShaderModulePool.Get(description.TaskShader);
    const VulkanShaderModuleState* meshModule = ShaderModulePool.Get(description.MeshShader);

    const VkPipelineVertexInputStateCreateInfo ciVertexInputState
    {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount    = renderPipelineState.NumberOfBindings,
        .pVertexBindingDescriptions       = renderPipelineState.NumberOfBindings ? renderPipelineState.Bindings : nullptr,
        .vertexAttributeDescriptionCount  = renderPipelineState.NumberOfAttributes,
        .pVertexAttributeDescriptions     = renderPipelineState.NumberOfAttributes ? renderPipelineState.Attributes : nullptr,
    };

    VkSpecializationMapEntry entries[EOS::SpecializationConstantDescription::MaxSecializationConstants]{};
    const VkSpecializationInfo specializationInfo = VkContext::GetPipelineShaderStageSpecializationInfo(description.SpecInfo, entries);

    // create pipeline layout
    {
        #define UPDATE_PUSH_CONSTANT_SIZE(sm, bit)                                  \
        if (sm)                                                                     \
        {                                                                           \
            pushConstantsSize = std::max(pushConstantsSize, sm->PushConstantsSize); \
            renderPipelineState.ShaderStageFlags |= bit;                            \
        }


        renderPipelineState.ShaderStageFlags = 0;
        uint32_t pushConstantsSize = 0;

        UPDATE_PUSH_CONSTANT_SIZE(vertModule, VK_SHADER_STAGE_VERTEX_BIT);
        UPDATE_PUSH_CONSTANT_SIZE(tescModule, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
        UPDATE_PUSH_CONSTANT_SIZE(teseModule, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
        UPDATE_PUSH_CONSTANT_SIZE(geomModule, VK_SHADER_STAGE_GEOMETRY_BIT);
        UPDATE_PUSH_CONSTANT_SIZE(fragModule, VK_SHADER_STAGE_FRAGMENT_BIT);
        UPDATE_PUSH_CONSTANT_SIZE(taskModule, VK_SHADER_STAGE_TASK_BIT_EXT);
        UPDATE_PUSH_CONSTANT_SIZE(meshModule, VK_SHADER_STAGE_MESH_BIT_EXT);

        #undef UPDATE_PUSH_CONSTANT_SIZE

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(VulkanPhysicalDevice, &props);
        CHECK(pushConstantsSize <= props.limits.maxPushConstantsSize, "Push constants size exceeded {} (max {} bytes)", pushConstantsSize, props.limits.maxPushConstantsSize);

        const VkDescriptorSetLayout dsls[] = {DescriptorSetLayout};
        const VkPushConstantRange range
        {
            .stageFlags = renderPipelineState.ShaderStageFlags,
            .offset = 0,
            .size = pushConstantsSize,
        };

        const VkPipelineLayoutCreateInfo createInfo
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = dsls,
            .pushConstantRangeCount = pushConstantsSize ? 1u : 0u,
            .pPushConstantRanges = pushConstantsSize ? &range : nullptr,
        };
        VK_ASSERT(vkCreatePipelineLayout(VulkanDevice, &createInfo, nullptr, &renderPipelineState.PipelineLayout));
        VK_ASSERT(VkDebug::SetDebugObjectName(VulkanDevice, VK_OBJECT_TYPE_PIPELINE_LAYOUT, reinterpret_cast<uint64_t>(renderPipelineState.PipelineLayout), fmt::format("Pipeline Layout: {}", renderPipelineDescription.DebugName).c_str()));
    }

    VK_ASSERT(VulkanPipelineBuilder()
    .DynamicState(VK_DYNAMIC_STATE_VIEWPORT)
    .DynamicState(VK_DYNAMIC_STATE_SCISSOR)
    .DynamicState(VK_DYNAMIC_STATE_DEPTH_BIAS)
    .DynamicState(VK_DYNAMIC_STATE_BLEND_CONSTANTS)
    .DynamicState(VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE)
    .DynamicState(VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE)
    .DynamicState(VK_DYNAMIC_STATE_DEPTH_COMPARE_OP)
    .DynamicState(VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE)
    .PrimitiveTypology(VkContext::TopologyToVkPrimitiveTopology(renderPipelineDescription.PipelineTopology))
    .RasterizationSamples(VkContext::GetVulkanSampleCountFlags(renderPipelineDescription.SamplesCount, VkContext::GetFramebufferMSAABitMask(VulkanPhysicalDevice)), renderPipelineDescription.MinSampleShading)
    .PolygonMode(VkContext::PolygonModeToVkPolygonMode(renderPipelineDescription.PolygonModeDescription))
    .StencilStateOps(VK_STENCIL_FACE_FRONT_BIT,
                       VkContext::StencilOpToVkStencilOp(renderPipelineDescription.FrontFaceStencil.StencilFailureOp),
                       VkContext::StencilOpToVkStencilOp(renderPipelineDescription.FrontFaceStencil.DepthStencilPassOp),
                       VkContext::StencilOpToVkStencilOp(renderPipelineDescription.FrontFaceStencil.DepthFailureOp),
                       VkContext::CompareOpToVkCompareOp(renderPipelineDescription.FrontFaceStencil.StencilCompareOp))
      .StencilStateOps(VK_STENCIL_FACE_BACK_BIT,
                       VkContext::StencilOpToVkStencilOp(renderPipelineDescription.BackFaceStencil.StencilFailureOp),
                       VkContext::StencilOpToVkStencilOp(renderPipelineDescription.BackFaceStencil.DepthStencilPassOp),
                       VkContext::StencilOpToVkStencilOp(renderPipelineDescription.BackFaceStencil.DepthFailureOp),
                       VkContext::CompareOpToVkCompareOp(renderPipelineDescription.BackFaceStencil.StencilCompareOp))
      .StencilMasks(VK_STENCIL_FACE_FRONT_BIT, 0xFF, renderPipelineDescription.FrontFaceStencil.WriteMask, renderPipelineDescription.FrontFaceStencil.ReadMask)
      .StencilMasks(VK_STENCIL_FACE_BACK_BIT, 0xFF, renderPipelineDescription.BackFaceStencil.WriteMask, renderPipelineDescription.BackFaceStencil.ReadMask)
      .ShaderStage(taskModule   ? VkContext::GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_TASK_BIT_EXT, taskModule->ShaderModule, renderPipelineDescription.EntryPointTask, &specializationInfo)
                                : VkPipelineShaderStageCreateInfo{.module = VK_NULL_HANDLE})
      .ShaderStage(meshModule   ? VkContext::GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_MESH_BIT_EXT, meshModule->ShaderModule, renderPipelineDescription.EntryPointMesh, &specializationInfo)
                                : VkContext::GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_VERTEX_BIT, vertModule->ShaderModule, renderPipelineDescription.EntryPointVert, &specializationInfo))
      .ShaderStage(VkContext::GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_FRAGMENT_BIT, fragModule->ShaderModule, renderPipelineDescription.EntryPointFrag, &specializationInfo))
      .ShaderStage(tescModule   ? VkContext::GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, tescModule->ShaderModule, renderPipelineDescription.EntryPointTesc, &specializationInfo)
                                : VkPipelineShaderStageCreateInfo{.module = VK_NULL_HANDLE})
      .ShaderStage(teseModule   ? VkContext::GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, teseModule->ShaderModule, renderPipelineDescription.EntryPointTese, &specializationInfo)
                                : VkPipelineShaderStageCreateInfo{.module = VK_NULL_HANDLE})
      .ShaderStage(geomModule   ? VkContext::GetPipelineShaderStageCreateInfo(VK_SHADER_STAGE_GEOMETRY_BIT, geomModule->ShaderModule, renderPipelineDescription.EntryPointGeom, &specializationInfo)
                                : VkPipelineShaderStageCreateInfo{.module = VK_NULL_HANDLE})
      .CullMode(VkContext::CullModeToVkCullMode(renderPipelineDescription.PipelineCullMode))
      .FrontFace(VkContext::WindingModeToVkFrontFace(renderPipelineDescription.FrontFaceWinding))
      .VertexInputState(ciVertexInputState)
      .ColorAttachments(colorBlendAttachmentStates, colorAttachmentFormats, numColorAttachments)
      .DepthAttachmentFormat(VkContext::FormatTovkFormat(renderPipelineDescription.DepthFormat))
      .StencilAttachmentFormat(VkContext::FormatTovkFormat(renderPipelineDescription.StencilFormat))
      .PatchControlPoints(renderPipelineDescription.PatchControlPoints)
      .Build(VulkanDevice, nullptr, renderPipelineState.PipelineLayout, &renderPipelineState.Pipeline, renderPipelineDescription.DebugName)); //TODO: Store the pipeline Cache

    return {this, RenderPipelinePool.Create(std::move(renderPipelineState))};
}

EOS::Holder<EOS::BufferHandle> VulkanContext::CreateBuffer(const EOS::BufferDescription& bufferDescription)
{
    CHECK(bufferDescription.Usage != EOS::BufferUsageFlags::None, "Invalid Buffer Usage");

    EOS::StorageType storageType = bufferDescription.Storage;

    //If we don't use a staging buffer device memory becomes host visible
    if (!UseStagingDevice && (storageType == EOS::StorageType::Device))
    {
        storageType = EOS::StorageType::HostVisible;
    }

    // Use staging device to transfer data into the buffer when the storage is private to the device
    VkBufferUsageFlags usageFlags = (storageType == EOS::StorageType::Device) ? VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT : 0;
    usageFlags |= VkContext::BufferUsageFlagsToVkBufferUsageFlags(bufferDescription.Usage);

    const VkMemoryPropertyFlags memFlags = VkContext::StorageTypeToVkMemoryPropertyFlags(storageType);

    EOS::BufferHandle handle = CreateBuffer(bufferDescription.Size, usageFlags, memFlags, bufferDescription.DebugName);

    if (bufferDescription.Data)
    {
        Upload(handle, bufferDescription.Data, bufferDescription.Size, 0);
    }

    return {this, handle};
}

EOS::Holder<EOS::TextureHandle> VulkanContext::CreateTexture(const EOS::TextureDescription& textureDescription)
{
    //Store copy to modify
    EOS::TextureDescription desc{textureDescription};

    const bool isDepthOrStencil = VkContext::IsDepthOrStencilFormat(desc.TextureFormat);
    VkFormat vkFormat = VkContext::FormatTovkFormat(desc.TextureFormat);;\

    if (isDepthOrStencil) { vkFormat = VkContext::GetClosestDepthStencilFormat(desc.TextureFormat, VulkanPhysicalDevice); }

    CHECK(vkFormat != VK_FORMAT_UNDEFINED, "Invalid VkFormat.");
    CHECK(desc.Type == EOS::ImageType::Image_2D || desc.Type == EOS::ImageType::Image_3D || desc.Type == EOS::ImageType::CubeMap || desc.Type == EOS::ImageType::Image_2D_Array || desc.Type == EOS::ImageType::CubeMap_Array , "Only 2D, 3D and Cubemaps are supported");
    CHECK(desc.NumberOfMipLevels >= 1, "The number of mip levels must be bigger then 0");
    if (desc.NumberOfSamples > 1)
    {
        CHECK(desc.NumberOfMipLevels == 1, "The mip levels for multisamples images should be 1");
        CHECK(desc.Type != EOS::ImageType::Image_3D, "3D images are not supported for multisampling");
    }
    CHECK(desc.NumberOfMipLevels <= EOS::CalculateNumberOfMipLevels(desc.TextureDimensions.Width, desc.TextureDimensions.Height), "The number of specified mip-levels is greater than the maximum possible");
    CHECK(desc.Usage != 0, "Usage flags are not set");

    // Use staging device to transfer data into the image when the storage is private to the device
    VkImageUsageFlags usageFlags = (desc.Storage == EOS::StorageType::Device) ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0;
    if (desc.Usage & EOS::TextureUsageFlags::Sampled)
    {
        usageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }

    if (desc.Usage & EOS::TextureUsageFlags::Storage)
    {
        CHECK(desc.NumberOfSamples <= 1, "Storage images cannot be multisampled");
        usageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;
    }

    if (desc.Usage & EOS::TextureUsageFlags::Attachment)
    {
        usageFlags |= VkContext::IsDepthOrStencilFormat(desc.TextureFormat) ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (desc.Storage == EOS::StorageType::Memoryless) usageFlags |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    }

    if (desc.Storage != EOS::StorageType::Memoryless)
    {
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    CHECK(usageFlags != 0, "Invalid Usage Flags");

    const VkMemoryPropertyFlags memFlags = VkContext::StorageTypeToVkMemoryPropertyFlags(desc.Storage);
    VkImageCreateFlags vkCreateFlags = 0;
    VkSampleCountFlagBits vkSamples = VK_SAMPLE_COUNT_1_BIT;


    //Handle Wrongly setting the image type by the number of layers
    //Handle Samples, layers and some creation flags
    switch (desc.Type)
    {
        case EOS::ImageType::Image_2D:
        case EOS::ImageType::Image_2D_Array:
        desc.Type = desc.NumberOfLayers > 1 ? EOS::ImageType::Image_2D_Array : EOS::ImageType::Image_2D;
        vkSamples = VkContext::GetVulkanSampleCountFlags(desc.NumberOfSamples, VkContext::GetFramebufferMSAABitMask(VulkanPhysicalDevice));
        break;
        case EOS::ImageType::CubeMap:
        case EOS::ImageType::CubeMap_Array:
        desc.Type = desc.NumberOfLayers > 1 ? EOS::ImageType::CubeMap_Array : EOS::ImageType::CubeMap;
        vkCreateFlags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        desc.NumberOfLayers *= 6;
        break;
        default:

        //Handle the case when a selected image type is not supported
        if (desc.Type != EOS::ImageType::Image_3D)
        {
            CHECK(false, "This is a unsupported image type");
            return {};
        }
    }

    VkImageViewType vkImageViewType = VulkanImage::ToImageViewType(desc.Type);
    VkImageType vkImageType = VulkanImage::ToImageType(desc.Type);
    const VkExtent3D vkExtent{desc.TextureDimensions.Width, desc.TextureDimensions.Height, desc.TextureDimensions.Depth};
    // TODO: Validate the VkExtent3D with the limits

    CHECK(desc.NumberOfMipLevels > 0, "The image must contain at least one mip-level");
    CHECK(desc.NumberOfLayers > 0, "The image must contain at least one layer");
    CHECK(vkSamples > 0, "The image must contain at least one sample");
    CHECK(vkExtent.width > 0, "The texture width is 0");
    CHECK(vkExtent.height > 0, "The texture height is 0");
    CHECK(vkExtent.depth > 0, "The texture depth is 0");

    VulkanImage image{};
    image.UsageFlags = usageFlags;
    image.Extent = vkExtent;
    image.ImageType = desc.Type;
    image.ImageFormat = vkFormat;
    image.Samples = vkSamples;
    image.Levels = desc.NumberOfMipLevels;
    image.Layers = desc.NumberOfLayers;


    const uint32_t numPlanes = VkContext::GetNumberOfImagePlanes(vkFormat);
    CHECK(numPlanes == 1, "Cannot handle multiplanar images at the moment");


    const VkImageCreateInfo ci
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = vkCreateFlags,
        .imageType = vkImageType,
        .format = vkFormat,
        .extent = vkExtent,
        .mipLevels = desc.NumberOfMipLevels,
        .arrayLayers = desc.NumberOfLayers,
        .samples = vkSamples,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usageFlags,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo vmaAllocInfo = {.usage = memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ? VMA_MEMORY_USAGE_CPU_TO_GPU : VMA_MEMORY_USAGE_AUTO};
    VK_ASSERT(vmaCreateImage(vmaAllocator, &ci, &vmaAllocInfo, &image.Image, &image.Allocation, nullptr));

    // handle memory-mapped buffers
    if (memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        vmaMapMemory(vmaAllocator, image.Allocation, &image.MappedPtr);
    }
    VK_ASSERT(VkDebug::SetDebugObjectName(VulkanDevice, VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(image.Image), desc.DebugName));

    // Get physical device's properties for the image's format
    vkGetPhysicalDeviceFormatProperties(VulkanPhysicalDevice, image.ImageFormat, &image.FormatProperties);

    VulkanImage::CreateImageView(image.ImageView, VulkanDevice, image.Image, desc.Type, vkFormat, VK_REMAINING_MIP_LEVELS, desc.NumberOfLayers, fmt::format("{} - Image View",desc.DebugName).c_str(), desc.Swizzle);
    if (image.UsageFlags & VK_IMAGE_USAGE_STORAGE_BIT && !desc.Swizzle.Identity())
    {
        VulkanImage::CreateImageView(image.ImageViewStorage, VulkanDevice, image.Image, desc.Type, vkFormat, VK_REMAINING_MIP_LEVELS, desc.NumberOfLayers, fmt::format("{} - Image View Storage",desc.DebugName).c_str(), desc.Swizzle);
    }

    EOS::TextureHandle handle = TexturePool.Create(std::move(image));

    if (desc.Data)
    {
        CHECK(desc.Type == EOS::ImageType::Image_2D || desc.Type == EOS::ImageType::CubeMap || desc.Type == EOS::ImageType::Image_2D_Array || desc.Type == EOS::ImageType::CubeMap_Array, "Can only upload data to the GPU is texture is 2D or Cubemap");
        CHECK(desc.DataNumberOfMipLevels <= desc.NumberOfMipLevels, "The specified numbers of mips that should be uploaded is bigger then the total allowed mips");

        const uint32_t numLayers = desc.Type == EOS::ImageType::CubeMap || desc.Type == EOS::ImageType::CubeMap_Array ? 6 : 1;

        const EOS::TextureRangeDescription textureRange
        {
            .Dimension = desc.TextureDimensions,
            .NumberOfLayers =  numLayers,
            .NumberOfMipLevels = desc.DataNumberOfMipLevels
        };

        Upload(handle, textureRange, desc.Data);

        if (desc.GenerateMipmaps)
        {
            GenerateMipmaps(handle);
        }
    }

    return {this, handle};
}

void VulkanContext::Destroy(EOS::TextureHandle handle)
{
    VulkanImage* image = TexturePool.Get(handle);
    CHECK(image, "Trying to destroy a already destroyed vulkan image");
    if (!image)
    {
        return;
    }
    //Destroy ImageView
    Defer(std::packaged_task<void()>([device = VulkanDevice, imageView = image->ImageView]() { vkDestroyImageView(device, imageView, nullptr); }));

    if (image->ImageViewStorage)
    {
        Defer(std::packaged_task<void()>([device = VulkanDevice, imageView = image->ImageViewStorage]() { vkDestroyImageView(device, imageView, nullptr); }));
    }

    for (size_t i{}; i != EOS_MAX_MIP_LEVELS; ++i)
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

    if (image->MappedPtr)
    {
        vmaUnmapMemory(vmaAllocator, image->Allocation);
    }

    if (image->Allocation)
    {
        Defer(std::packaged_task<void()>([vma = vmaAllocator, image = image->Image, allocation = image->Allocation](){ vmaDestroyImage(vma, image, allocation); }));
    }

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

void VulkanContext::Destroy(EOS::RenderPipelineHandle handle)
{
    VulkanRenderPipelineState* renderPipelineState = RenderPipelinePool.Get(handle);

    if (!renderPipelineState)
    {
        EOS::Logger->warn("Tried to destroy a non-valid RenderPipelineState");
        return;
    }

    //TODO: Questionable solution ....
    free(renderPipelineState->SpecConstantDataStorage);

    Defer(std::packaged_task<void()>([device = VulkanDevice, pipeline = renderPipelineState->Pipeline]() { vkDestroyPipeline(device, pipeline, nullptr); }));
    Defer(std::packaged_task<void()>([device = VulkanDevice, layout = renderPipelineState->PipelineLayout]() { vkDestroyPipelineLayout(device, layout, nullptr); }));

    RenderPipelinePool.Destroy(handle);
}

void VulkanContext::Destroy(EOS::BufferHandle handle)
{
    VulkanBuffer* buf = BufferPool.Get(handle);
    if (!buf)
    {
        return;
    }

    if (buf->MappedPtr)
    {
        vmaUnmapMemory(vmaAllocator, buf->VMAAllocation);
    }

    Defer(std::packaged_task<void()>([vma = vmaAllocator, buffer = buf->VulkanVkBuffer, allocation = buf->VMAAllocation]()
    {
          vmaDestroyBuffer(vma, buffer, allocation);
    }));

    BufferPool.Destroy(handle);
}

void VulkanContext::Upload(EOS::BufferHandle handle, const void *data, size_t size, size_t offset)
{
    CHECK(data, "The data you want to upload should be valid");
    CHECK(size, "Data size should be non-zero");

    VulkanStagingBuffer->BufferSubData(handle, offset, size, data);
}

void VulkanContext::Upload(EOS::TextureHandle handle, const EOS::TextureRangeDescription &range, const void *data)
{
    CHECK(data, "The texture data you want to upload is not valid");

    VulkanImage* texture = TexturePool.Get(handle);
    CHECK(texture, "The texture of this handle is not valid");

    //Validate the image range
    const uint32_t numberOfLayers = std::max(range.NumberOfLayers, 1u);
    const uint32_t texWidth = std::max(texture->Extent.width >> range.MipLevel, 1u);
    const uint32_t texHeight = std::max(texture->Extent.height >> range.MipLevel, 1u);
    const uint32_t texDepth = std::max(texture->Extent.depth >> range.MipLevel, 1u);

    CHECK(range.Dimension.Width > 0 && range.Dimension.Height > 0 || range.Dimension.Depth > 0 || range.NumberOfLayers > 0 || range.NumberOfMipLevels > 0, "The specified range is out of range");
    CHECK(range.MipLevel > numberOfLayers, "range.mipLevel is bigger then the texture mip levels");
    CHECK(range.Dimension.Width < texWidth && range.Dimension.Height < texHeight && range.Dimension.Depth < texDepth, "Dimension out of range");
    CHECK(range.Offset.X < texWidth - range.Dimension.Width && range.Offset.Y < texHeight - range.Dimension.Height && range.Offset.Z < texDepth - range.Dimension.Depth, "range dimensions exceed texture dimensions");

    if (VulkanImage::ToImageType(texture->ImageType) == VK_IMAGE_TYPE_3D)
    {
        const VkOffset3D offset {range.Offset.X, range.Offset.Y, range.Offset.Z};
        const VkExtent3D extent {range.Dimension.Width, range.Dimension.Height, range.Dimension.Depth};
        assert(false);

        //TODO: Add 3D Image Support
        //VulkanStagingBuffer->ImageData3D(*texture, offset,extent, texture->ImageFormat, data);
    }
    else
    {
        const VkRect2D imageRegion
        {
            .offset = {.x = range.Offset.X, .y = range.Offset.Y},
            .extent = {.width = range.Dimension.Width, .height = range.Dimension.Height},
        };

        VulkanStagingBuffer->ImageData2D(*texture, imageRegion, range.MipLevel, range.NumberOfMipLevels, range.Layer, range.NumberOfLayers, texture->ImageFormat, data);
    }
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

void VulkanContext::GrowDescriptorPool(uint32_t maxTextures, uint32_t maxSamplers, uint32_t maxAccelStructs)
{
    //TODO: Check for our limits in vkPhysicalDeviceVulkan12Properties
    //Device Properties
    //VkPhysicalDeviceProperties2 vkPhysicalDeviceProperties2;
    //VkPhysicalDeviceDriverProperties vkPhysicalDeviceDriverProperties;
    //uint32_t SDKApiVersion{};
    //vkEnumerateInstanceVersion(&SDKApiVersion);
    //const uint32_t SDKMinor = VK_API_VERSION_MINOR(SDKApiVersion);
    //VkContext::GetPhysicalDeviceProperties(vkPhysicalDeviceProperties2, vkPhysicalDeviceDriverProperties, VulkanPhysicalDevice, SDKMinor);

    //CHECK(maxTextures <= vkPhysicalDeviceVulkan12Properties.maxDescriptorSetUpdateAfterBindSampledImages, "Max Textures exceeded, Current:{}, Max:{}", maxTextures, vkPhysicalDeviceVulkan12Properties_.maxDescriptorSetUpdateAfterBindSampledImages);
    //CHECK(maxSamplers <= vkPhysicalDeviceVulkan12Properties.maxDescriptorSetUpdateAfterBindSamplers, "Max Samplers exceeded, Current:{}, Max:{}", maxSamplers);

    CurrentMaxTextures      = maxTextures;
    CurrentMaxSamplers      = maxSamplers;
    CurrentMaxAccelStructs  = maxAccelStructs;

    if (DescriptorSetLayout != VK_NULL_HANDLE)
    {
        Defer(std::packaged_task<void()>([device = VulkanDevice, dsl = DescriptorSetLayout]() { vkDestroyDescriptorSetLayout(device, dsl, nullptr); }));
    }

    if (DescriptorPool != VK_NULL_HANDLE)
    {
        Defer(std::packaged_task<void()>([device = VulkanDevice, dp = DescriptorPool]() { vkDestroyDescriptorPool(device, dp, nullptr); }));
    }

    // create default descriptor set layout which is going to be shared by graphics pipelines
    VkShaderStageFlags stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    if (HasRaytracingPipeline)
    {
        stageFlags |= VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    }

    const VkDescriptorSetLayoutBinding bindings[EOS::Bindings::Count]
    {
        VkContext::GetDSLBinding(EOS::Bindings::Textures, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxTextures, stageFlags),
        VkContext::GetDSLBinding(EOS::Bindings::Samplers, VK_DESCRIPTOR_TYPE_SAMPLER, maxSamplers, stageFlags),
        VkContext::GetDSLBinding(EOS::Bindings::StorageImages, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxTextures, stageFlags),
        VkContext::GetDSLBinding(EOS::Bindings::AccelerationStructures, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, maxAccelStructs, stageFlags),
    };

    constexpr uint32_t flags = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;

    VkDescriptorBindingFlags bindingFlags[EOS::Bindings::Count];

    for (int i{}; i < EOS::Bindings::Count; ++i)
    {
        bindingFlags[i] = flags;
    }

    const VkDescriptorSetLayoutBindingFlagsCreateInfo setLayoutBindingFlagsCI
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT,
        .bindingCount = static_cast<uint32_t>(HasAccelerationStructure ? EOS::Bindings::Count : EOS::Bindings::Count - 1),
        .pBindingFlags = bindingFlags,
    };

    const VkDescriptorSetLayoutCreateInfo dslci
    {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &setLayoutBindingFlagsCI,
        .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT,
        .bindingCount = static_cast<uint32_t>(HasAccelerationStructure ? EOS::Bindings::Count : EOS::Bindings::Count - 1),
        .pBindings = bindings,
    };

    VK_ASSERT(vkCreateDescriptorSetLayout(VulkanDevice, &dslci, nullptr, &DescriptorSetLayout));
    VK_ASSERT(VkDebug::SetDebugObjectName(VulkanDevice, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, reinterpret_cast<uint64_t>(DescriptorSetLayout), "Descriptor Set Layout: VulkanContext::DescriptorSetLayout"));

    {
        // create default descriptor pool and allocate 1 descriptor set
        const VkDescriptorPoolSize poolSizes[EOS::Bindings::Count]
        {
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, maxTextures},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_SAMPLER, maxSamplers},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, maxTextures},
            VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, maxAccelStructs},
        };

        const VkDescriptorPoolCreateInfo ci
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
            .maxSets = 1,
            .poolSizeCount = static_cast<uint32_t>(HasRaytracingPipeline ? EOS::Bindings::Count : EOS::Bindings::Count - 1),
            .pPoolSizes = poolSizes,
        };
        VK_ASSERT(vkCreateDescriptorPool(VulkanDevice, &ci, nullptr, &DescriptorPool));

        const VkDescriptorSetAllocateInfo ai
        {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = DescriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &DescriptorSetLayout,
        };
        VK_ASSERT(vkAllocateDescriptorSets(VulkanDevice, &ai, &DescriptorSet));
    }
}

void VulkanContext::BindDefaultDescriptorSet(VkCommandBuffer commandBuffer, VkPipelineBindPoint bindPoint, VkPipelineLayout layout) const
{
    vkCmdBindDescriptorSets(commandBuffer, bindPoint, layout, 0, 1, &DescriptorSet, 0, nullptr);
}

VkDevice VulkanContext::GetDevice() const
{
    return VulkanDevice;
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
    Configuration.EnableValidationLayers = foundLayer;

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

    if (Configuration.EnableValidationLayers)
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

void VulkanContext::CreateAllocator()
{
    const VmaVulkanFunctions functions
    {
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
        .vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties,
        .vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties,
        .vkAllocateMemory = vkAllocateMemory,
        .vkFreeMemory = vkFreeMemory,
        .vkMapMemory = vkMapMemory,
        .vkUnmapMemory = vkUnmapMemory,
        .vkFlushMappedMemoryRanges = vkFlushMappedMemoryRanges,
        .vkInvalidateMappedMemoryRanges = vkInvalidateMappedMemoryRanges,
        .vkBindBufferMemory = vkBindBufferMemory,
        .vkBindImageMemory = vkBindImageMemory,
        .vkGetBufferMemoryRequirements = vkGetBufferMemoryRequirements,
        .vkGetImageMemoryRequirements = vkGetImageMemoryRequirements,
        .vkCreateBuffer = vkCreateBuffer,
        .vkDestroyBuffer = vkDestroyBuffer,
        .vkCreateImage = vkCreateImage,
        .vkDestroyImage = vkDestroyImage,
        .vkCmdCopyBuffer = vkCmdCopyBuffer,
        .vkGetBufferMemoryRequirements2KHR = vkGetBufferMemoryRequirements2,
        .vkGetImageMemoryRequirements2KHR = vkGetImageMemoryRequirements2,
        .vkBindBufferMemory2KHR = vkBindBufferMemory2,
        .vkBindImageMemory2KHR = vkBindImageMemory2,
        .vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2,
        .vkGetDeviceBufferMemoryRequirements = vkGetDeviceBufferMemoryRequirements,
        .vkGetDeviceImageMemoryRequirements = vkGetDeviceImageMemoryRequirements,
    };

    const VmaAllocatorCreateInfo createInfo
    {
        .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
        .physicalDevice = VulkanPhysicalDevice,
        .device = VulkanDevice,
        .preferredLargeHeapBlockSize = 0,
        .pAllocationCallbacks = nullptr,
        .pDeviceMemoryCallbacks = nullptr,
        .pHeapSizeLimit = nullptr,
        .pVulkanFunctions = &functions,
        .instance = VulkanInstance,
        .vulkanApiVersion = VK_API_VERSION_1_4, //TODO: Make 1.3 compatible
    };

    VK_ASSERT(vmaCreateAllocator(&createInfo, &vmaAllocator));
}

void VulkanContext::GenerateMipmaps(EOS::TextureHandle handle)
{
    if (handle.Empty()) { return; }

    const VulkanImage* texture = TexturePool.Get(handle);
    if (texture->Levels <= 1) { return; }

    CommandBufferData* commandbuffer = VulkanCommandPool->AcquireCommandBuffer();
    texture->GenerateMipmaps(commandbuffer->VulkanCommandBuffer);

    // ReSharper disable once CppNoDiscardExpression
    auto result = VulkanCommandPool->Submit(*commandbuffer);
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

void VulkanContext::WaitOnDeferredTasks() const
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

EOS::BufferHandle VulkanContext::CreateBuffer(VkDeviceSize bufferSize, VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memFlags, const char *debugName)
{
    CHECK(bufferSize > 0, "The buffer you want to create needs to be bigger then 0");

    //TODO: Check maxUniformBufferRange -> VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
    //TODO: Check maxStorageBufferRange -> VK_BUFFER_USAGE_FLAG_BITS_MAX_ENUM

    VulkanBuffer buffer
    {
      .BufferSize = bufferSize,
      .VkUsageFlags = usageFlags,
      .VkMemoryFlags = memFlags,
    };

    const VkBufferCreateInfo createInfo
    {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .pNext = nullptr,
      .flags = 0,
      .size = bufferSize,
      .usage = usageFlags,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .queueFamilyIndexCount = 0,
      .pQueueFamilyIndices = nullptr,
    };

    VmaAllocationCreateInfo vmaAllocInfo{};

    if (memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        vmaAllocInfo =
        {
            .flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
            .requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            .preferredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        };

        // Check if coherent buffer is available.
        VK_ASSERT(vkCreateBuffer(VulkanDevice, &createInfo, nullptr, &buffer.VulkanVkBuffer));


        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(VulkanDevice, buffer.VulkanVkBuffer, &requirements);

        //Reset the Buffer
        vkDestroyBuffer(VulkanDevice, buffer.VulkanVkBuffer, nullptr);
        buffer.VulkanVkBuffer = VK_NULL_HANDLE;


        if (requirements.memoryTypeBits & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        {
            vmaAllocInfo.requiredFlags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            buffer.IsCoherentMemory = true;
        }
    }

    vmaCreateBuffer(vmaAllocator, &createInfo, &vmaAllocInfo, &buffer.VulkanVkBuffer, &buffer.VMAAllocation, nullptr);

    // handle memory-mapped buffers
    if (memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        vmaMapMemory(vmaAllocator, buffer.VMAAllocation, &buffer.MappedPtr);
    }


    CHECK(buffer.VulkanVkBuffer != VK_NULL_HANDLE, "Could not create buffer");
    VK_ASSERT(VkDebug::SetDebugObjectName(VulkanDevice, VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(buffer.VulkanVkBuffer), debugName));

    // handle shader access
    if (usageFlags & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    {
        const VkBufferDeviceAddressInfo addressInfo
        {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = buffer.VulkanVkBuffer,
        };

        buffer.VulkanDeviceAddress = vkGetBufferDeviceAddress(VulkanDevice, &addressInfo);
        CHECK(buffer.VulkanDeviceAddress, "Could not get Buffer Device Address");
  }

  return BufferPool.Create(std::move(buffer));
}