#include "shaderCache.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>
#include <type_traits>

namespace EOS::ShaderCache
{
    namespace
    {
        constexpr uint32_t kMagic = 0x50534F45;     // "EOSP"
        constexpr uint32_t kFormatVersion = 7;      // bump whenever the layout or the meaning of what is written below changes

        constexpr uint32_t kDerivedMagic = 0x44534F45;  // "EOSD"
        constexpr uint32_t kDerivedFormatVersion = 1;

        constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ull;
        constexpr uint64_t kFnvPrime = 0x100000001b3ull;

        // One read for the whole file: going through istreambuf_iterator is a call per byte, which in debug builds
        // made reading the cache take longer than the compile it saves.
        [[nodiscard]] bool ReadWholeFile(const std::filesystem::path& path, std::string& outData)
        {
            std::ifstream file(path, std::ios::in | std::ios::binary | std::ios::ate);
            if (!file.is_open()) return false;

            const std::streamsize size = file.tellg();
            if (size < 0) return false;

            outData.resize(static_cast<size_t>(size));
            file.seekg(0);
            return size == 0 || static_cast<bool>(file.read(outData.data(), size));
        }

        class BinaryWriter final
        {
        public:
            template<typename T>
            requires std::is_trivially_copyable_v<T>
            void Write(const T& value)
            {
                Buffer.append(reinterpret_cast<const char*>(&value), sizeof(T));
            }

            void WriteString(std::string_view text)
            {
                Write(static_cast<uint32_t>(text.size()));
                Buffer.append(text);
            }

            void WritePath(const std::filesystem::path& path)
            {
                const std::u8string utf8 = path.generic_u8string();
                WriteString(std::string_view(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
            }

            void WriteVarying(const ShaderVarying& varying)
            {
                WriteString(varying.Name);
                WriteString(varying.Semantic);
                Write(varying.SemanticIndex);
                Write(varying.Location);
                Write(varying.ComponentType);
                Write(varying.ComponentCount);
            }

            std::string Buffer;
        };

        class BinaryReader final
        {
        public:
            explicit BinaryReader(std::string_view data) : Data(data) {}

            template<typename T>
            requires std::is_trivially_copyable_v<T>
            [[nodiscard]] bool Read(T& outValue)
            {
                if (Data.size() - Offset < sizeof(T)) return false;
                std::memcpy(&outValue, Data.data() + Offset, sizeof(T));
                Offset += sizeof(T);
                return true;
            }

            [[nodiscard]] bool ReadString(std::string& outText)
            {
                uint32_t size = 0;
                if (!Read(size) || Data.size() - Offset < size) return false;
                outText.assign(Data.data() + Offset, size);
                Offset += size;
                return true;
            }

            [[nodiscard]] bool ReadPath(std::filesystem::path& outPath)
            {
                std::string utf8;
                if (!ReadString(utf8)) return false;
                outPath = std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
                return true;
            }

            [[nodiscard]] bool ReadVarying(ShaderVarying& outVarying)
            {
                return ReadString(outVarying.Name)
                    && ReadString(outVarying.Semantic)
                    && Read(outVarying.SemanticIndex)
                    && Read(outVarying.Location)
                    && Read(outVarying.ComponentType)
                    && Read(outVarying.ComponentCount);
            }

            [[nodiscard]] bool ReadBytes(void* outData, size_t size)
            {
                if (Data.size() - Offset < size) return false;
                std::memcpy(outData, Data.data() + Offset, size);
                Offset += size;
                return true;
            }

            // Guards element counts read from the file against corrupt data before anything gets resized.
            [[nodiscard]] bool ReadCount(uint32_t& outCount, size_t minimumElementSize)
            {
                return Read(outCount) && static_cast<uint64_t>(outCount) * minimumElementSize <= Data.size() - Offset;
            }

            [[nodiscard]] bool AtEnd() const
            {
                return Offset == Data.size();
            }

        private:
            std::string_view Data;
            size_t Offset = 0;
        };

        void WriteProgram(BinaryWriter& writer, const CompiledShaderProgram& program)
        {
            const ShaderProgramDescription& description = program.Description;
            writer.WriteString(description.Module);

            writer.Write(static_cast<uint32_t>(description.EntryPoints.size()));
            for (const std::string& entryPoint : description.EntryPoints) writer.WriteString(entryPoint);

            writer.Write(static_cast<uint32_t>(description.Defines.size()));
            for (const ShaderMacro& define : description.Defines)
            {
                writer.WriteString(define.Name);
                writer.WriteString(define.Value);
            }

            writer.Write(static_cast<uint32_t>(description.LinkModules.size()));
            for (const std::string& linkModule : description.LinkModules) writer.WriteString(linkModule);

            writer.Write(static_cast<uint32_t>(description.SourceModules.size()));
            for (const ShaderSourceModule& sourceModule : description.SourceModules)
            {
                writer.WriteString(sourceModule.Name);
                writer.WriteString(sourceModule.Source);
            }

            writer.WriteString(program.CompilerVersion);
            writer.Write(program.PushConstantSize);

            writer.Write(static_cast<uint32_t>(program.SpecializationConstants.size()));
            for (const ShaderSpecializationConstant& constant : program.SpecializationConstants)
            {
                writer.WriteString(constant.Name);
                writer.Write(constant.ConstantID);
                writer.Write(constant.Type);
            }

            writer.Write(static_cast<uint32_t>(program.ResourceBindings.size()));
            for (const ShaderResourceBinding& binding : program.ResourceBindings)
            {
                writer.WriteString(binding.Name);
                writer.Write(binding.Set);
                writer.Write(binding.Binding);
                writer.Write(binding.Type);
                writer.Write(binding.StageMask);
            }

            writer.Write(static_cast<uint32_t>(program.Dependencies.size()));
            for (const ShaderSourceDependency& dependency : program.Dependencies)
            {
                writer.WritePath(dependency.Path);
                writer.Write(dependency.ContentHash);
            }

            writer.Write(static_cast<uint32_t>(program.EntryPoints.size()));
            for (const ShaderEntryPoint& entryPoint : program.EntryPoints)
            {
                writer.WriteString(entryPoint.Name);
                writer.Write(entryPoint.Stage);
                writer.Write(entryPoint.ThreadGroupSize);
                writer.Write(entryPoint.PushConstantSize);

                writer.Write(static_cast<uint32_t>(entryPoint.Inputs.size()));
                for (const ShaderVarying& varying : entryPoint.Inputs) writer.WriteVarying(varying);

                writer.Write(static_cast<uint32_t>(entryPoint.Outputs.size()));
                for (const ShaderVarying& varying : entryPoint.Outputs) writer.WriteVarying(varying);

                writer.Write(static_cast<uint32_t>(entryPoint.Spirv.size()));
                writer.Buffer.append(reinterpret_cast<const char*>(entryPoint.Spirv.data()), entryPoint.Spirv.size() * sizeof(uint32_t));
            }

            const ShaderPassReflection& pass = program.Pass;
            writer.Write(pass.IsPass);
            writer.Write(pass.DispatchThreads);
            writer.WriteString(pass.DispatchSizeOf);
            writer.Write(pass.DrawScene);
            writer.Write(pass.Cull);
            writer.Write(pass.DepthClamp);
            writer.Write(static_cast<uint32_t>(pass.Fragments.size()));
            for (const ShaderPassFragment& fragment : pass.Fragments)
            {
                writer.WriteString(fragment.EntryPoint);
                writer.Write(fragment.Materials);
            }
            writer.Write(static_cast<uint32_t>(pass.Fields.size()));
            for (const ShaderPassField& field : pass.Fields)
            {
                writer.WriteString(field.Name);
                writer.Write(field.Kind);
                writer.Write(field.Direction);
                writer.Write(field.Offset);
                writer.Write(field.Optional);
                writer.WriteString(field.Format);
                writer.Write(field.Scale);
                writer.Write(field.Width);
                writer.Write(field.Height);
                writer.Write(field.Layers);
                writer.WriteString(field.SizeOf);
                writer.WriteString(field.Bypass);
                writer.Write(field.Clear);
                writer.Write(field.BufferSize);
                writer.WriteString(field.BufferType);
                writer.Write(field.DepthCompare);
                writer.Write(field.SamplerFilter);
                writer.Write(field.SamplerAddress);
                writer.Write(field.Minimum);
                writer.Write(field.Maximum);
                writer.Write(field.Default);
                writer.Write(static_cast<uint32_t>(field.EnumNames.size()));
                for (size_t i = 0; i < field.EnumNames.size(); ++i)
                {
                    writer.WriteString(field.EnumNames[i]);
                    writer.Write(field.EnumValues[i]);
                }
            }
        }

        [[nodiscard]] bool ReadProgram(BinaryReader& reader, CompiledShaderProgram& outProgram)
        {
            ShaderProgramDescription& description = outProgram.Description;
            if (!reader.ReadString(description.Module)) return false;

            uint32_t count = 0;
            if (!reader.ReadCount(count, sizeof(uint32_t))) return false;
            description.EntryPoints.resize(count);
            for (std::string& entryPoint : description.EntryPoints)
            {
                if (!reader.ReadString(entryPoint)) return false;
            }

            if (!reader.ReadCount(count, 2 * sizeof(uint32_t))) return false;
            description.Defines.resize(count);
            for (ShaderMacro& define : description.Defines)
            {
                if (!reader.ReadString(define.Name) || !reader.ReadString(define.Value)) return false;
            }

            if (!reader.ReadCount(count, sizeof(uint32_t))) return false;
            description.LinkModules.resize(count);
            for (std::string& linkModule : description.LinkModules)
            {
                if (!reader.ReadString(linkModule)) return false;
            }

            if (!reader.ReadCount(count, 2 * sizeof(uint32_t))) return false;
            description.SourceModules.resize(count);
            for (ShaderSourceModule& sourceModule : description.SourceModules)
            {
                if (!reader.ReadString(sourceModule.Name) || !reader.ReadString(sourceModule.Source)) return false;
            }

            if (!reader.ReadString(outProgram.CompilerVersion) || !reader.Read(outProgram.PushConstantSize)) return false;

            if (!reader.ReadCount(count, sizeof(uint32_t))) return false;
            outProgram.SpecializationConstants.resize(count);
            for (ShaderSpecializationConstant& constant : outProgram.SpecializationConstants)
            {
                if (!reader.ReadString(constant.Name) || !reader.Read(constant.ConstantID) || !reader.Read(constant.Type)) return false;
            }

            if (!reader.ReadCount(count, sizeof(uint32_t))) return false;
            outProgram.ResourceBindings.resize(count);
            for (ShaderResourceBinding& binding : outProgram.ResourceBindings)
            {
                if (!reader.ReadString(binding.Name)
                    || !reader.Read(binding.Set)
                    || !reader.Read(binding.Binding)
                    || !reader.Read(binding.Type)
                    || !reader.Read(binding.StageMask))
                {
                    return false;
                }
            }

            if (!reader.ReadCount(count, sizeof(uint32_t))) return false;
            outProgram.Dependencies.resize(count);
            for (ShaderSourceDependency& dependency : outProgram.Dependencies)
            {
                if (!reader.ReadPath(dependency.Path) || !reader.Read(dependency.ContentHash)) return false;
            }

            if (!reader.ReadCount(count, sizeof(uint32_t))) return false;
            outProgram.EntryPoints.resize(count);
            for (ShaderEntryPoint& entryPoint : outProgram.EntryPoints)
            {
                if (!reader.ReadString(entryPoint.Name)
                    || !reader.Read(entryPoint.Stage)
                    || !reader.Read(entryPoint.ThreadGroupSize)
                    || !reader.Read(entryPoint.PushConstantSize))
                {
                    return false;
                }

                if (!reader.ReadCount(count, sizeof(uint32_t))) return false;
                entryPoint.Inputs.resize(count);
                for (ShaderVarying& varying : entryPoint.Inputs)
                {
                    if (!reader.ReadVarying(varying)) return false;
                }

                if (!reader.ReadCount(count, sizeof(uint32_t))) return false;
                entryPoint.Outputs.resize(count);
                for (ShaderVarying& varying : entryPoint.Outputs)
                {
                    if (!reader.ReadVarying(varying)) return false;
                }

                if (!reader.ReadCount(count, sizeof(uint32_t))) return false;
                entryPoint.Spirv.resize(count);
                if (!reader.ReadBytes(entryPoint.Spirv.data(), entryPoint.Spirv.size() * sizeof(uint32_t))) return false;
            }

            ShaderPassReflection& pass = outProgram.Pass;
            if (!reader.Read(pass.IsPass) || !reader.Read(pass.DispatchThreads) || !reader.ReadString(pass.DispatchSizeOf)
                || !reader.Read(pass.DrawScene) || !reader.Read(pass.Cull) || !reader.Read(pass.DepthClamp)
                || !reader.ReadCount(count, sizeof(uint32_t) + sizeof(uint8_t)))
            {
                return false;
            }
            pass.Fragments.resize(count);
            for (ShaderPassFragment& fragment : pass.Fragments)
            {
                if (!reader.ReadString(fragment.EntryPoint) || !reader.Read(fragment.Materials)) return false;
            }

            if (!reader.ReadCount(count, 4 * sizeof(uint32_t))) return false;
            pass.Fields.resize(count);
            for (ShaderPassField& field : pass.Fields)
            {
                if (!reader.ReadString(field.Name)
                    || !reader.Read(field.Kind)
                    || !reader.Read(field.Direction)
                    || !reader.Read(field.Offset)
                    || !reader.Read(field.Optional)
                    || !reader.ReadString(field.Format)
                    || !reader.Read(field.Scale)
                    || !reader.Read(field.Width)
                    || !reader.Read(field.Height)
                    || !reader.Read(field.Layers)
                    || !reader.ReadString(field.SizeOf)
                    || !reader.ReadString(field.Bypass)
                    || !reader.Read(field.Clear)
                    || !reader.Read(field.BufferSize)
                    || !reader.ReadString(field.BufferType)
                    || !reader.Read(field.DepthCompare)
                    || !reader.Read(field.SamplerFilter)
                    || !reader.Read(field.SamplerAddress)
                    || !reader.Read(field.Minimum)
                    || !reader.Read(field.Maximum)
                    || !reader.Read(field.Default)
                    || !reader.ReadCount(count, sizeof(uint32_t) + sizeof(int64_t)))
                {
                    return false;
                }

                field.EnumNames.resize(count);
                field.EnumValues.resize(count);
                for (uint32_t i = 0; i < count; ++i)
                {
                    if (!reader.ReadString(field.EnumNames[i]) || !reader.Read(field.EnumValues[i])) return false;
                }
            }

            return true;
        }
    }

    uint64_t HashBytes(const void* data, size_t size, uint64_t seed)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        uint64_t hash = seed ^ kFnvOffsetBasis;
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= kFnvPrime;
        }

        return hash;
    }

    uint64_t HashString(std::string_view text, uint64_t seed)
    {
        // Hash the length too, so ("ab", "c") and ("a", "bc") hash differently when chained.
        const uint64_t size = text.size();
        return HashBytes(text.data(), text.size(), HashBytes(&size, sizeof(size), seed));
    }

    uint64_t HashDescription(const ShaderProgramDescription& description)
    {
        uint64_t hash = HashString(description.Module, 0);
        for (const std::string& entryPoint : description.EntryPoints) hash = HashString(entryPoint, hash);

        hash = HashString("|defines|", hash);
        for (const ShaderMacro& define : description.Defines)
        {
            hash = HashString(define.Name, hash);
            hash = HashString(define.Value, hash);
        }

        hash = HashString("|link|", hash);
        for (const std::string& linkModule : description.LinkModules) hash = HashString(linkModule, hash);

        hash = HashString("|source|", hash);
        for (const ShaderSourceModule& sourceModule : description.SourceModules)
        {
            hash = HashString(sourceModule.Name, hash);
            hash = HashString(sourceModule.Source, hash);
        }

        return hash;
    }

    bool HashFile(const std::filesystem::path& path, uint64_t& outHash)
    {
        std::string content;
        if (!ReadWholeFile(path, content)) return false;

        outHash = HashBytes(content.data(), content.size(), 0);
        return true;
    }

    std::filesystem::path GetFilePath(const std::filesystem::path& cacheDirectory, const ShaderProgramDescription& description)
    {
        std::string fileName = description.Module;
        if (!description.EntryPoints.empty() || !description.Defines.empty() || !description.LinkModules.empty() || !description.SourceModules.empty())
        {
            char suffix[24];
            std::snprintf(suffix, sizeof(suffix), "-%016llx", static_cast<unsigned long long>(HashDescription(description)));
            fileName += suffix;
        }

        return cacheDirectory / (fileName + FileExtension);
    }

    namespace
    {
        // Writes next to the destination and renames over it, so a reader (a running application, or another build
        // invoking the tool) never sees a half-written file.
        [[nodiscard]] bool WriteFileAtomically(const std::filesystem::path& path, const std::string& bytes, std::string& outError)
        {
            std::error_code errorCode;
            std::filesystem::create_directories(path.parent_path(), errorCode);
            if (errorCode)
            {
                outError = "cannot create shader cache directory " + path.parent_path().string() + ": " + errorCode.message();
                return false;
            }

            std::filesystem::path temporaryPath = path;
            temporaryPath += ".tmp";
            {
                std::ofstream file(temporaryPath, std::ios::out | std::ios::binary | std::ios::trunc);
                if (!file.is_open())
                {
                    outError = "cannot open " + temporaryPath.string() + " for writing";
                    return false;
                }

                file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                if (!file.good())
                {
                    outError = "failed writing " + temporaryPath.string();
                    return false;
                }
            }

            std::filesystem::rename(temporaryPath, path, errorCode);
            if (errorCode)
            {
                outError = "cannot move " + temporaryPath.string() + " to " + path.string() + ": " + errorCode.message();
                std::filesystem::remove(temporaryPath, errorCode);
                return false;
            }

            return true;
        }
    }

    bool Write(const std::filesystem::path& path, uint64_t optionsHash, const CompiledShaderProgram& program, std::string& outError)
    {
        BinaryWriter writer;
        writer.Write(kMagic);
        writer.Write(kFormatVersion);
        writer.Write(optionsHash);
        writer.Write(HashDescription(program.Description));
        WriteProgram(writer, program);

        return WriteFileAtomically(path, writer.Buffer, outError);
    }

    bool Read(const std::filesystem::path& path, uint64_t optionsHash, const ShaderProgramDescription& description, CompiledShaderProgram& outProgram, std::string& outError)
    {
        std::string data;
        if (!ReadWholeFile(path, data))
        {
            outError = "no cached program at " + path.string();
            return false;
        }

        BinaryReader reader(data);

        uint32_t magic = 0;
        uint32_t formatVersion = 0;
        uint64_t storedOptionsHash = 0;
        uint64_t storedDescriptionHash = 0;
        if (!reader.Read(magic) || magic != kMagic)
        {
            outError = path.string() + " is not an EOS shader cache file";
            return false;
        }

        if (!reader.Read(formatVersion) || formatVersion != kFormatVersion)
        {
            outError = path.string() + " uses cache format " + std::to_string(formatVersion) + ", expected " + std::to_string(kFormatVersion);
            return false;
        }

        if (!reader.Read(storedOptionsHash) || storedOptionsHash != optionsHash)
        {
            outError = path.string() + " was compiled with different compiler options (other build configuration or Slang version)";
            return false;
        }

        if (!reader.Read(storedDescriptionHash) || storedDescriptionHash != HashDescription(description))
        {
            outError = path.string() + " holds a different program";
            return false;
        }

        CompiledShaderProgram program;
        if (!ReadProgram(reader, program) || !reader.AtEnd())
        {
            outError = path.string() + " is truncated or corrupt";
            return false;
        }

        if (program.Description != description)
        {
            outError = path.string() + " holds a different program";
            return false;
        }

        outProgram = std::move(program);
        return true;
    }

    bool AreDependenciesUpToDate(const CompiledShaderProgram& program)
    {
        return AreDependenciesUpToDate(program.Dependencies);
    }

    bool AreDependenciesUpToDate(const std::vector<ShaderSourceDependency>& dependencies)
    {
        if (dependencies.empty()) return false;

        for (const ShaderSourceDependency& dependency : dependencies)
        {
            uint64_t hash = 0;
            if (!HashFile(dependency.Path, hash) || hash != dependency.ContentHash) return false;
        }

        return true;
    }

    bool WriteDerived(const std::filesystem::path& path, const DerivedEntry& entry, std::string& outError)
    {
        BinaryWriter writer;
        writer.Write(kDerivedMagic);
        writer.Write(kDerivedFormatVersion);
        writer.Write(entry.Key);
        writer.WritePath(entry.Source);
        writer.Write(static_cast<uint32_t>(entry.Dependencies.size()));
        for (const ShaderSourceDependency& dependency : entry.Dependencies)
        {
            writer.WritePath(dependency.Path);
            writer.Write(dependency.ContentHash);
        }
        writer.Write(static_cast<uint64_t>(entry.Payload.size()));
        writer.Buffer.append(entry.Payload);

        return WriteFileAtomically(path, writer.Buffer, outError);
    }

    bool ReadDerived(const std::filesystem::path& path, uint64_t key, DerivedEntry& outEntry)
    {
        std::string data;
        if (!ReadWholeFile(path, data)) return false;

        BinaryReader reader(data);

        uint32_t magic = 0;
        uint32_t formatVersion = 0;
        DerivedEntry entry;
        uint32_t numberOfDependencies = 0;
        if (!reader.Read(magic) || magic != kDerivedMagic || !reader.Read(formatVersion) || formatVersion != kDerivedFormatVersion) return false;
        if (!reader.Read(entry.Key) || entry.Key != key || !reader.ReadPath(entry.Source)) return false;
        if (!reader.ReadCount(numberOfDependencies, sizeof(uint32_t) + sizeof(uint64_t))) return false;

        entry.Dependencies.resize(numberOfDependencies);
        for (ShaderSourceDependency& dependency : entry.Dependencies)
        {
            if (!reader.ReadPath(dependency.Path) || !reader.Read(dependency.ContentHash)) return false;
        }

        uint64_t payloadSize = 0;
        if (!reader.Read(payloadSize) || payloadSize > data.size()) return false;

        entry.Payload.resize(payloadSize);
        if (!reader.ReadBytes(entry.Payload.data(), entry.Payload.size()) || !reader.AtEnd()) return false;

        outEntry = std::move(entry);
        return true;
    }
}
