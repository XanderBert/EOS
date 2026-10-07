#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "graphFileDescription.h"
#include "renderGraph.h"

// The data layer of the render graph, in the style of Falcor's render passes and graph scripts. C++ registers pass
// types: named pins (the textures and buffers a pass reads and writes), properties, and the function that records the
// pass. A graph file (YAML) says which passes a frame has, their property values and how their pins are connected. It is
// what a node editor would save: passes are nodes, edges are wires.
//
// Graph files are authored in the editor and built into the application: the build turns each one into a header,
// .generated/graphs/<file>.h, with the file as constants (graphFileDescription.h). With EOS_GRAPH_TOOLS (the editor),
// GraphFile reads the YAML itself, and GraphFile::Reload() picks up changes to it. Without (a shipping build), it uses
// the generated constants: no YAML is read or parsed.
//
//     passes:
//       Geometry: { type: GBuffer }
//       Lighting: { type: DeferredLighting, debugView: Normals }
//       Present:  { type: Present }
//     edges:
//       - perFrame        -> Geometry.perFrame
//       - Geometry.albedo -> Lighting.albedo
//       - Geometry.normal -> Lighting.normal
//       - Lighting.output -> Present.input
//       - Present.output  -> swapchain
//
// "Pass.pin" names a pin. A name without a dot is a resource the application hands to GraphFile::AddTo(), such as the
// swapchain or a buffer it owns. Outputs nothing reads are culled, so passes that do not lead to an application resource
// do not run.
namespace EOS
{
    class GraphFile;

    // Storage of PassRegistry and GraphFile. Only defined in renderGraphFile.cpp.
    struct PassRegistryData;
    struct GraphFileData;

    enum class PinDirection : uint8_t
    {
        Input,          // reads what another pass or the application provides
        Output,         // writes a resource the graph creates, or the application resource it is connected to
        InputOutput,    // changes the resource it receives and passes it on
    };

    /**
     * @brief How a pass uses the resource on a pin. Decides the barriers and, for raster passes, the attachments: color
     *        and depth targets are attached in pin order.
     */
    enum class PinUsage : uint8_t
    {
        // Textures.
        Sampled,            // read by shaders
        Storage,            // read and written by shaders
        ColorTarget,        // rendered into: cleared as an output, loaded as an input-output
        DepthTarget,        // depth tested and written: cleared as an output, loaded as an input-output
        DepthTest,          // depth tested without writes (an input); shaders may sample it as well

        // Buffers.
        ReadBuffer,         // read by shaders
        WriteBuffer,        // read and written by shaders
        IndirectBuffer,     // draw or dispatch arguments
        TransferBuffer,     // written by copies or cmdUpdateBuffer
    };

    [[nodiscard]] constexpr bool IsBufferUsage(PinUsage usage)
    {
        return usage >= PinUsage::ReadBuffer;
    }

    /**
     * @brief Settings of an output whose resource the graph creates. A graph file can override the format and scale.
     */
    struct OutputOptions final
    {
        float Scale = 1.0f;                         // of the swapchain size, or of SizeOf's texture
        const char* SizeOf = "";                    // an input pin: this output is that texture's size times Scale
        std::array<float, 4> ClearColor{0.0f, 0.0f, 0.0f, 0.0f};
        float ClearDepth = 1.0f;
        uint8_t LayerCount = 1;                     // layers a target renders at once (a shader picks one per triangle)
        const char* BypassFrom = "";                // an input pin: what readers get instead while the pass is disabled
    };

    struct PinDescription final
    {
        const char* Name = "";
        PinDirection Direction = PinDirection::Input;
        PinUsage Usage = PinUsage::Sampled;
        bool Optional = false;                      // inputs that may stay unconnected
        Format TextureFormat = Format::Invalid;     // outputs; Format::Invalid is the swapchain's format
        size_t BufferSize = 0;                      // buffer outputs
        OutputOptions Output{};
    };

    // Pin descriptions for PassTypeDescription::Pins.
    namespace Pin
    {
        // An output with this format follows the swapchain's.
        constexpr Format SwapchainFormat = Format::Invalid;

        [[nodiscard]] constexpr PinDescription Sampled(const char* name) { return {.Name = name, .Usage = PinUsage::Sampled}; }
        [[nodiscard]] constexpr PinDescription DepthTest(const char* name) { return {.Name = name, .Usage = PinUsage::DepthTest}; }
        [[nodiscard]] constexpr PinDescription ReadBuffer(const char* name) { return {.Name = name, .Usage = PinUsage::ReadBuffer}; }
        [[nodiscard]] constexpr PinDescription IndirectBuffer(const char* name) { return {.Name = name, .Usage = PinUsage::IndirectBuffer}; }

        // An input the pass can do without: PassData then returns an invalid handle for it.
        [[nodiscard]] constexpr PinDescription Optional(PinDescription pin)
        {
            pin.Optional = true;
            return pin;
        }

        [[nodiscard]] constexpr PinDescription ColorOutput(const char* name, Format format, const OutputOptions& options = {})
        {
            return {.Name = name, .Direction = PinDirection::Output, .Usage = PinUsage::ColorTarget, .TextureFormat = format, .Output = options};
        }

        [[nodiscard]] constexpr PinDescription DepthOutput(const char* name, Format format, const OutputOptions& options = {})
        {
            return {.Name = name, .Direction = PinDirection::Output, .Usage = PinUsage::DepthTarget, .TextureFormat = format, .Output = options};
        }

        [[nodiscard]] constexpr PinDescription StorageOutput(const char* name, Format format, const OutputOptions& options = {})
        {
            return {.Name = name, .Direction = PinDirection::Output, .Usage = PinUsage::Storage, .TextureFormat = format, .Output = options};
        }

        [[nodiscard]] constexpr PinDescription BufferOutput(const char* name, size_t size, const OutputOptions& options = {})
        {
            return {.Name = name, .Direction = PinDirection::Output, .Usage = PinUsage::WriteBuffer, .BufferSize = size, .Output = options};
        }

        [[nodiscard]] constexpr PinDescription TransferBufferOutput(const char* name, size_t size, const OutputOptions& options = {})
        {
            return {.Name = name, .Direction = PinDirection::Output, .Usage = PinUsage::TransferBuffer, .BufferSize = size, .Output = options};
        }

        [[nodiscard]] constexpr PinDescription ColorInOut(const char* name) { return {.Name = name, .Direction = PinDirection::InputOutput, .Usage = PinUsage::ColorTarget}; }
        [[nodiscard]] constexpr PinDescription DepthInOut(const char* name) { return {.Name = name, .Direction = PinDirection::InputOutput, .Usage = PinUsage::DepthTarget}; }
        [[nodiscard]] constexpr PinDescription StorageInOut(const char* name) { return {.Name = name, .Direction = PinDirection::InputOutput, .Usage = PinUsage::Storage}; }
        [[nodiscard]] constexpr PinDescription BufferInOut(const char* name) { return {.Name = name, .Direction = PinDirection::InputOutput, .Usage = PinUsage::WriteBuffer}; }
    }

    enum class PropertyType : uint8_t
    {
        Bool,
        Int,
        Float,
        Float2,
        Float3,
        Float4,
        Choice,         // one of a list of names; the value is its index
    };

    struct PropertyValue final
    {
        glm::vec4 Float{0.0f};                      // Float to Float4: the components they use
        int32_t Int = 0;                            // Int, and Choice (the index)
        bool Bool = false;
    };

    struct PropertyDescription final
    {
        const char* Name = "";
        PropertyType Type = PropertyType::Float;
        PropertyValue Default{};
        float Min = 0.0f;                           // the range a UI offers; none when Min == Max
        float Max = 0.0f;
        std::span<const char* const> Choices{};     // Choice: the names, in index order
    };

    // Property descriptions for PassTypeDescription::Properties.
    namespace Property
    {
        [[nodiscard]] inline PropertyDescription Bool(const char* name, bool value)
        {
            return {.Name = name, .Type = PropertyType::Bool, .Default = {.Bool = value}};
        }

        [[nodiscard]] inline PropertyDescription Int(const char* name, int32_t value, int32_t min = 0, int32_t max = 0)
        {
            return {.Name = name, .Type = PropertyType::Int, .Default = {.Int = value}, .Min = static_cast<float>(min), .Max = static_cast<float>(max)};
        }

        [[nodiscard]] inline PropertyDescription Float(const char* name, float value, float min = 0.0f, float max = 0.0f)
        {
            return {.Name = name, .Type = PropertyType::Float, .Default = {.Float = glm::vec4(value, 0.0f, 0.0f, 0.0f)}, .Min = min, .Max = max};
        }

        [[nodiscard]] inline PropertyDescription Float2(const char* name, glm::vec2 value, float min = 0.0f, float max = 0.0f)
        {
            return {.Name = name, .Type = PropertyType::Float2, .Default = {.Float = glm::vec4(value, 0.0f, 0.0f)}, .Min = min, .Max = max};
        }

        [[nodiscard]] inline PropertyDescription Float3(const char* name, glm::vec3 value, float min = 0.0f, float max = 0.0f)
        {
            return {.Name = name, .Type = PropertyType::Float3, .Default = {.Float = glm::vec4(value, 0.0f)}, .Min = min, .Max = max};
        }

        [[nodiscard]] inline PropertyDescription Float4(const char* name, glm::vec4 value, float min = 0.0f, float max = 0.0f)
        {
            return {.Name = name, .Type = PropertyType::Float4, .Default = {.Float = value}, .Min = min, .Max = max};
        }

        // choices is copied when the pass type is registered; graph files name the choice, not its index.
        [[nodiscard]] inline PropertyDescription Choice(const char* name, std::span<const char* const> choices, int32_t value = 0)
        {
            return {.Name = name, .Type = PropertyType::Choice, .Default = {.Int = value}, .Choices = choices};
        }
    }

    /**
     * @brief What a pass function gets besides the PassContext: the resources on its pins and its property values.
     *        Pins and properties are looked up by name, so a typo is reported when the pass runs.
     */
    class PassData final
    {
    public:
        [[nodiscard]] const char* Name() const;                     // of the pass in the graph file

        // Invalid for an optional input that is not connected.
        [[nodiscard]] GraphTexture Texture(std::string_view pin) const;
        [[nodiscard]] GraphBuffer Buffer(std::string_view pin) const;

        [[nodiscard]] bool Bool(std::string_view property) const;
        [[nodiscard]] int32_t Int(std::string_view property) const; // also a Choice's index
        [[nodiscard]] float Float(std::string_view property) const;
        [[nodiscard]] glm::vec2 Float2(std::string_view property) const;
        [[nodiscard]] glm::vec3 Float3(std::string_view property) const;
        [[nodiscard]] glm::vec4 Float4(std::string_view property) const;

    private:
        friend struct GraphFileData;
        PassData(const GraphFileData& file, uint32_t pass) : File(file), Pass(pass) {}

        [[nodiscard]] uint32_t FindPin(std::string_view pin) const;
        [[nodiscard]] const PropertyValue& FindProperty(std::string_view property, PropertyType type) const;

        const GraphFileData& File;
        uint32_t Pass;
    };

    using PassTypeFunction = std::function<void(PassContext& context, const PassData& data)>;

    struct PassTypeDescription final
    {
        const char* Name = "";
        PassKind Kind = PassKind::Compute;
        std::vector<PinDescription> Pins{};
        std::vector<PropertyDescription> Properties{};
        PassTypeFunction Execute;                   // recorded like a PassBuilder::Execute function
    };

    /**
     * @brief The pass types graph files can use. Register every type before loading a graph file that uses it.
     */
    class PassRegistry final
    {
    public:
        PassRegistry();
        ~PassRegistry();
        DELETE_COPY_MOVE(PassRegistry)

        // Mistakes in a description (a duplicate name, an output sized like a pin that is not an input) are bugs in
        // the C++ code and stop the program.
        void Register(PassTypeDescription description);

    private:
        friend class GraphFile;
        std::unique_ptr<PassRegistryData> Types;
    };

    /**
     * @brief A resource the application hands to a graph file, under the name the file uses for it.
     */
    struct GraphResource final
    {
        GraphResource(const char* name, GraphTexture texture) : Name(name), Texture(texture) {}
        GraphResource(const char* name, GraphBuffer buffer) : Name(name), Buffer(buffer) {}

        const char* Name;
        GraphTexture Texture{};
        GraphBuffer Buffer{};
    };

    /**
     * @brief A pass of a graph file, for UIs that edit it.
     */
    struct GraphFilePass final
    {
        const char* Name = "";
        const char* Type = "";
        bool* Enabled = nullptr;
        uint32_t PropertyCount = 0;
    };

    /**
     * @brief A property of a pass of a graph file, for UIs that edit it.
     */
    struct GraphFileProperty final
    {
        const char* Name = "";
        PropertyType Type = PropertyType::Float;
        PropertyValue* Value = nullptr;
        float Min = 0.0f;
        float Max = 0.0f;
        std::span<const char* const> Choices{};
    };

    /**
     * @brief A graph file, from its generated header: `GraphFile file{registry, DepthOfFieldGraph};`.
     *
     * A file with errors is reported (path:line:column, like a compiler) and the last version without errors stays in
     * use, so a typo while editing never takes the frame down.
     */
    class GraphFile final
    {
    public:
        // file has to outlive the GraphFile (the generated constants do).
        GraphFile(const PassRegistry& registry, const GraphFileDescription& file);
        ~GraphFile();
        DELETE_COPY_MOVE(GraphFile)

        /**
         * @brief Adds the file's passes to graph, each after the passes it reads from. Passes added to graph before and
         *        after this call run before and after them. Call it once per frame.
         * @param resources The application's resources the file connects to, by name.
         */
        void AddTo(RenderGraph& graph, std::initializer_list<GraphResource> resources);

        /**
         * @brief Loads the YAML file again when it changed on disk since it was last read, like
         *        IContext::ReloadShaders() (the examples bind both to the same key). Can be called at any time: the new
         *        version is used from the next AddTo on. A version with errors is reported and the one in use stays.
         *        Does nothing without EOS_GRAPH_TOOLS.
         * @return Whether a new version was loaded.
         */
        bool Reload();

        // Whether a version of the file without errors was loaded.
        [[nodiscard]] bool IsLoaded() const;

        // This frame's resource on a pin ("Pass.pin"), after AddTo; invalid when there is no such pin or the pass did
        // not run. Lets code add passes that read what the file's passes wrote.
        [[nodiscard]] GraphTexture GetTexture(std::string_view pin) const;
        [[nodiscard]] GraphBuffer GetBuffer(std::string_view pin) const;

        // Changes made from code or a UI last until the file is reloaded. Return false when there is no such pass or
        // property (a property of another type counts as none).
        bool SetEnabled(std::string_view pass, bool enabled);
        bool SetProperty(std::string_view pass, std::string_view property, bool value);
        bool SetProperty(std::string_view pass, std::string_view property, int32_t value);
        bool SetProperty(std::string_view pass, std::string_view property, float value);

        // For UIs that edit the file's passes. Pointers stay valid until a reloaded version is put in use (AddTo).
        [[nodiscard]] uint32_t GetPassCount() const;
        [[nodiscard]] GraphFilePass GetPass(uint32_t pass);
        [[nodiscard]] GraphFileProperty GetProperty(uint32_t pass, uint32_t property);

    private:
        std::unique_ptr<GraphFileData> File;
    };
}
