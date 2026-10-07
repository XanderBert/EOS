#include "../../Common/App.h"
#include ".generated/compute.h"

struct Resources final
{
    EOS::ShaderProgramHolder ComputeShader;
    EOS::BufferHolder ComputeBuffer;
    EOS::ComputePipelineHolder ComputePipeline;
};

Resources Handles;

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
    Handles.ComputeShader = App.Context->CreateShaderProgram({.Module = "compute"});

    constexpr ComputePayload initialPayload
    {
        .lhs = 70,
        .rhs = 9,
        .result = 0,
    };

    Handles.ComputeBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::HostVisible,
        .Size      = sizeof(ComputePayload),
        .Data      = &initialPayload,
        .DebugName = "ComputePayloadBuffer",
    });

    const EOS::ComputePipelineDescription computePipelineDescription
    {
        .ComputeShader = {Handles.ComputeShader, "computeMain"},
        .DebugName = "Compute Validation Pipeline",
    };
    Handles.ComputePipeline = App.Context->CreateComputePipeline(computePipelineDescription);

    App.Run([&]()
    {
        // A graph without a swapchain image: Execute() submits without presenting.
        EOS::RenderGraph& graph = *App.Graph;
        const EOS::GraphBuffer payload = graph.ImportBuffer(Handles.ComputeBuffer, "ComputePayloadBuffer");

        graph.AddComputePass("Compute Validation").Write(payload).Execute([payload](EOS::PassContext& pass)
        {
            cmdBindComputePipeline(pass.Cmd, Handles.ComputePipeline);
            cmdPushConstants(pass.Cmd, ComputePushConstants{.payload = pass.Address(payload)});
            cmdDispatchThreadGroups(pass.Cmd, {1, 1, 1});
        });

        App.Context->Wait(graph.Execute());

        const auto* computeData = reinterpret_cast<const ComputePayload*>(App.Context->GetMappedPtr(Handles.ComputeBuffer));
        if (computeData->result == computeData->lhs * computeData->rhs)
        {
            EOS::Logger->info("Compute validation success: {} * {} = {}", computeData->lhs, computeData->rhs, computeData->result);
            App.Exit();
        }
        else
        {
            EOS::Logger->error("Compute validation failed: {} * {} != {}", computeData->lhs, computeData->rhs, computeData->result);
        }
    });

    Handles = {};

    return 0;
}