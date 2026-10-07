#include "../../Common/App.h"
#include ".generated/compute.h"

int main()
{
    ExampleApp App{{.Name = "EOS - Compute Pipeline"}};

    constexpr ComputePayload initialPayload
    {
        .lhs = 70,
        .rhs = 9,
        .result = 0,
    };

    // Host visible, so the result can be read back after the frame.
    const EOS::BufferHolder payloadBuffer = App.Context->CreateBuffer({
        .Usage     = EOS::BufferUsageFlags::StorageFlag,
        .Storage   = EOS::StorageType::HostVisible,
        .Size      = sizeof(ComputePayload),
        .Data      = &initialPayload,
        .DebugName = "ComputePayloadBuffer",
    });

    // The frame is graphs/compute.yaml; its pass is shaders/compute.slang. This example checks the result, so it runs
    // its own loop instead of App.Run(file).
    EOS::GraphFile& graphFile = App.LoadGraphFile("compute");

    App.Run([&]()
    {
        // A graph without a swapchain image: Execute() submits without presenting.
        EOS::RenderGraph& graph = *App.Graph;
        graphFile.AddTo(graph, {{"payload", graph.ImportBuffer(payloadBuffer, "ComputePayloadBuffer")}});

        App.Context->Wait(graph.Execute());

        const auto* computeData = reinterpret_cast<const ComputePayload*>(App.Context->GetMappedPtr(payloadBuffer));
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

    return 0;
}
