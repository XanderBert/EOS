#include "shaderCache.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>
#include <type_traits>

namespace EOS::ShaderCache
{
    namespace
    {
        constexpr uint32_t kMagic = 0x50534F45;     // "EOSP"
        constexpr uint32_t kFormatVersion = 2;      // bump whenever the layout written below changes

        constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ull;
        constexpr uint64_t kFnvPrime = 0x100000001b3ull;

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

        return hash;
    }

    bool HashFile(const std::filesystem::path& path, uint64_t& outHash)
    {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file.is_open()) return false;

        const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (file.bad()) return false;

        outHash = HashBytes(content.data(), content.size(), 0);
        return true;
    }

    std::filesystem::path GetFilePath(const std::filesystem::path& cacheDirectory, const ShaderProgramDescription& description)
    {
        std::string fileName = description.Module;
        if (!description.EntryPoints.empty() || !description.Defines.empty())
        {
            char suffix[24];
            std::snprintf(suffix, sizeof(suffix), "-%016llx", static_cast<unsigned long long>(HashDescription(description)));
            fileName += suffix;
        }

        return cacheDirectory / (fileName + FileExtension);
    }

    bool Write(const std::filesystem::path& path, uint64_t optionsHash, const CompiledShaderProgram& program, std::string& outError)
    {
        BinaryWriter writer;
        writer.Write(kMagic);
        writer.Write(kFormatVersion);
        writer.Write(optionsHash);
        writer.Write(HashDescription(program.Description));
        WriteProgram(writer, program);

        std::error_code errorCode;
        std::filesystem::create_directories(path.parent_path(), errorCode);
        if (errorCode)
        {
            outError = "cannot create shader cache directory " + path.parent_path().string() + ": " + errorCode.message();
            return false;
        }

        // Write next to the destination and rename over it, so a reader (a running application, or another build
        // invoking the tool) never sees a half-written file.
        std::filesystem::path temporaryPath = path;
        temporaryPath += ".tmp";
        {
            std::ofstream file(temporaryPath, std::ios::out | std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                outError = "cannot open " + temporaryPath.string() + " for writing";
                return false;
            }

            file.write(writer.Buffer.data(), static_cast<std::streamsize>(writer.Buffer.size()));
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

    bool Read(const std::filesystem::path& path, uint64_t optionsHash, const ShaderProgramDescription& description, CompiledShaderProgram& outProgram, std::string& outError)
    {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file.is_open())
        {
            outError = "no cached program at " + path.string();
            return false;
        }

        const std::string data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
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
        if (program.Dependencies.empty()) return false;

        for (const ShaderSourceDependency& dependency : program.Dependencies)
        {
            uint64_t hash = 0;
            if (!HashFile(dependency.Path, hash) || hash != dependency.ContentHash) return false;
        }

        return true;
    }
}
