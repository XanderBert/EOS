#include "../../Common/App.h"
#include "EOS.h"
#include "imgui.h"
#include "logger.h"
#include "shaders/shaderCompiler.h"
#include "utils.h"

struct ComputePayload final
{
    uint32_t lhs{};
    uint32_t rhs{};
    uint32_t result{};
    uint32_t pad{};
};

struct ComputePushConstants final
{
    uint64_t payloadPtr{};
};

int main()
{
    const EOS::ContextCreationDescription contextDescr
    {
        .Config                 = { .EnableValidationLayers = true },
        .PreferredHardwareType  = EOS::HardwareDeviceType::Discrete,
        .ApplicationName        = "EOS - Compute Pipeline",
    };

    ExampleAppDescription appDescription
    {
        .contextDescription = contextDescr,
    };

    ExampleApp App{appDescription};
    EOS::ShaderModuleHolder ComputeShader = App.Context->CreateShaderModule("compute", EOS::ShaderStage::Compute);

    constexpr ComputePayload initialPayload
    {
        .lhs = 70,
        .rhs = 9,
        .result = 0,
    };

    EOS::BufferHolder ComputeBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::HostVisible,
        .Size      = sizeof(ComputePayload),
        .Data      = &initialPayload,
        .DebugName = "ComputePayloadBuffer",
    });

    const EOS::ComputePipelineDescription computePipelineDescription
    {
        .ComputeShader = ComputeShader,
        .DebugName = "Compute Validation Pipeline",
    };
    EOS::ComputePipelineHolder computePipelineHandle = App.Context->CreateComputePipeline(computePipelineDescription);

    const ComputePushConstants computePushConstants
    {
        .payloadPtr = App.Context->GetGPUAddress(ComputeBuffer),
    };

    App.Run([&]()
    {

        const auto* computeData = reinterpret_cast<const ComputePayload*>(App.Context->GetMappedPtr(ComputeBuffer));
        if (computeData->result == computeData->lhs * computeData->rhs)
        {
            EOS::Logger->info("Compute validation success: {} * {} = {}", computeData->lhs, computeData->rhs, computeData->result);
            App.Exit();
            return;
        }

        EOS::ICommandBuffer& cmdBuffer = App.Context->AcquireCommandBuffer();
        cmdPushMarker(cmdBuffer, "Compute Validation", 0xfff59d00);
        cmdBindComputePipeline(cmdBuffer, computePipelineHandle);
        cmdPushConstants(cmdBuffer, computePushConstants);
        cmdDispatchThreadGroups(cmdBuffer, {1, 1, 1});
        cmdPopMarker(cmdBuffer);

        App.Context->Submit(cmdBuffer, {});
    });

    return 0;
}