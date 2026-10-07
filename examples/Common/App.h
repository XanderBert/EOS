#pragma once
#include <concepts>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "EOS.h"
#include "flyCamera.h"
#include "renderGraph.h"
#include "renderGraphFile.h"
#include "scene.h"
#include "sun.h"
#include "UI/UI.h"

struct ExampleAppDescription final
{
    const char* Name = "EOS";                               // the window's title, unless contextDescription has one
    EOS::ContextCreationDescription contextDescription{};
};

// What the examples share: the window, the context, the render graph, the pass types graph files can use (the engine's
// nodes and Slang passes, and what an example registers itself) and the UI. An example is its graph files and their
// Slang passes: main.cpp runs them with Run().
class ExampleApp final
{
public:
    explicit ExampleApp(ExampleAppDescription appDescription)
    :   ExampleApp(Named(appDescription), Prepared{})
    {
    }

    DELETE_COPY_MOVE(ExampleApp)

    // The example's graph file src/graphs/<name>.yaml, whose pass types are registered in Passes:
    // LoadGraphFile("depthOfField"). The '-' key reloads it, with the shaders.
    EOS::GraphFile& LoadGraphFile(std::string_view name)
    {
        const std::filesystem::path path = std::filesystem::path(EOS_PROJECT_GRAPH_PATH) / (std::string(name) + ".yaml");
        return *GraphFiles.emplace_back(std::make_unique<EOS::GraphFile>(Passes, path));
    }

    // Shaders and graph files whose sources changed since they were last loaded.
    void Reload()
    {
        Context->ReloadShaders();
        for (const std::unique_ptr<EOS::GraphFile>& graphFile : GraphFiles) graphFile->Reload();
    }

    // Declares the UI of this frame (declareWidgets calls EOS::UI functions) and draws it on top of target. Textures the
    // UI shows have to be imported into the graph and added with .Sample() on the returned pass.
    template <typename Function>
    EOS::PassBuilder AddUIPass(EOS::GraphTexture target, Function&& declareWidgets)
    {
        UIRenderer->NewFrame();
        std::forward<Function>(declareWidgets)();

        EOS::PassBuilder pass = Graph->AddRasterPass("UI").Color(EOS::Load(target));
        pass.Execute([this](EOS::PassContext& context)
        {
            UIRenderer->Render(context.Cmd);
        });
        return pass;
    }

    /**
     * @brief Renders the example's graph file src/graphs/<name>.yaml every frame until the window closes: its passes
     *        draw into the swapchain, under a panel with its passes, their properties and a preview of any texture they
     *        write. The scene, the camera and the passes all come from the file and its Slang passes.
     */
    void Run(std::string_view graphName)
    {
        Run({graphName});
    }

    // As Run(graphName), with a choice of graph files in the panel; the first one is shown first.
    void Run(std::initializer_list<std::string_view> graphNames)
    {
        struct ShownFile final
        {
            std::string Name;
            EOS::GraphFile* File = nullptr;
            std::unique_ptr<EOS::UI::GraphFilePanel> Panel;
        };

        std::vector<ShownFile> files;
        std::vector<std::string> labels;
        for (const std::string_view name : graphNames)
        {
            EOS::GraphFile& file = LoadGraphFile(name);
            files.push_back({.Name = std::string(name), .File = &file, .Panel = std::make_unique<EOS::UI::GraphFilePanel>(Context.get(), file)});
            labels.push_back("graphs/" + std::string(name) + ".yaml");
        }
        std::vector<const char*> labelPointers;
        for (const std::string& label : labels) labelPointers.push_back(label.c_str());

        int shown = 0;
        while (!Window.ShouldClose() && !ShouldExit)
        {
            Window.Poll();

            ShownFile& file = files[shown];
            EOS::RenderGraph& graph = *Graph;
            const EOS::GraphTexture backbuffer = graph.ImportSwapchain();
            file.File->AddTo(graph, {{"swapchain", backbuffer}});

            const EOS::GraphTexture preview = file.Panel->AddPreviewPass(graph);
            EOS::PassBuilder uiPass = AddUIPass(backbuffer, [&]
            {
                EOS::UI::SetNextWindowSize(420, 640);
                EOS::UI::Begin(Name);
                if (files.size() > 1) EOS::UI::Combo("Graph", &shown, labelPointers.data(), static_cast<int>(labelPointers.size()));
                else EOS::UI::Text("%s", labelPointers[0]);
                file.Panel->Declare();
                EOS::UI::End();
            });
            if (preview.Valid()) uiPass.Sample(preview);

            graph.Execute();
        }
    }

    // Calls renderLoop every frame until the window closes, for examples that build their frame in C++.
    template <typename Function>
        requires std::invocable<Function&>
    void Run(Function&& renderLoop)
    {
        while (!Window.ShouldClose() && !ShouldExit)
        {
            Window.Poll();
            std::forward<Function>(renderLoop)();
        }
    }

    void Exit()
    {
        ShouldExit = true;
    }

    EOS::Window Window;
    std::unique_ptr<EOS::IContext> Context;
    std::unique_ptr<EOS::RenderGraph> Graph;        // destroyed before the context, which its textures belong to
    EOS::PassRegistry Passes{Context.get()};        // the pass types graph files can use; loads the Slang ones
    std::unique_ptr<EOS::UI::Renderer> UIRenderer;

private:
    struct Prepared final {};

    [[nodiscard]] static ExampleAppDescription& Named(ExampleAppDescription& appDescription)
    {
        if (!appDescription.contextDescription.ApplicationName) appDescription.contextDescription.ApplicationName = appDescription.Name;
        return appDescription;
    }

    ExampleApp(ExampleAppDescription& appDescription, Prepared)
    :   Window(appDescription.contextDescription)
    ,   Context(EOS::CreateContextWithSwapChain(appDescription.contextDescription))
    ,   Name(appDescription.contextDescription.ApplicationName)
    {
        // '-' reloads the shaders and graph files whose sources changed.
        Window.OnKey([this](int key, int, int action, int)
        {
            if (key == GLFW_KEY_MINUS && action == GLFW_PRESS) Reload();
        });

        Graph = std::make_unique<EOS::RenderGraph>(Context.get());

        // Graph files can load glTF scenes (paths relative to the repository's data folder), fly a camera and light
        // the scene with a sun.
        EOS::RegisterGltfScenePass(Passes, Context.get(), EOS_DATA_PATH);
        EOS::RegisterFlyCameraPass(Passes, Window);
        EOS::RegisterSunPass(Passes);

        UIRenderer = std::make_unique<EOS::UI::Renderer>(Context.get(), Window);
    }

    const char* Name;
    bool ShouldExit = false;
    std::vector<std::unique_ptr<EOS::GraphFile>> GraphFiles;    // after Passes, which they use
};
