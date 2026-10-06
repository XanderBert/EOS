#include "shaderCodegen.h"

#if defined(EOS_SHADER_TOOLS)
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <set>

namespace EOS::ShaderCodegen
{
    namespace
    {
        using Kind = slang::TypeReflection::Kind;
        using ScalarType = slang::TypeReflection::ScalarType;

        // Declared in eos/core.slang as CppExportAttribute; Slang looks attributes up without the suffix.
        constexpr const char* kExportAttribute = "CppExport";

        [[nodiscard]] std::filesystem::path FromUtf8(const char* text)
        {
            const std::string_view view(text);
            return std::filesystem::path(std::u8string(view.begin(), view.end()));
        }

        [[nodiscard]] std::string Qualify(const std::string& cppNamespace, const std::string& name)
        {
            return cppNamespace.empty() ? name : cppNamespace + "::" + name;
        }

        // Where the mirror of a module lives.
        struct ModuleLocation final
        {
            std::string Namespace;
            std::string Include;        // ".generated/eos/material.h"
            std::string SourceName;     // "eos/material.slang"
        };

        [[nodiscard]] bool LocateModule(slang::IModule* module, const std::vector<CppExportRoot>& roots, ModuleLocation& outLocation)
        {
            const char* filePath = module->getFilePath();
            if (!filePath) return false;

            const std::filesystem::path path = std::filesystem::absolute(FromUtf8(filePath)).lexically_normal();
            for (const CppExportRoot& root : roots)
            {
                const std::filesystem::path relative = path.lexically_relative(std::filesystem::absolute(root.ShaderDirectory).lexically_normal());
                if (relative.empty() || *relative.begin() == "..") continue;

                std::filesystem::path header = relative;
                header.replace_extension(".h");
                outLocation = {.Namespace = root.Namespace, .Include = ".generated/" + header.generic_string(), .SourceName = relative.generic_string()};
                return true;
            }

            return false;
        }

        [[nodiscard]] bool IsTypeDeclaration(slang::DeclReflection* decl)
        {
            return decl->getKind() == slang::DeclReflection::Kind::Struct || decl->getKind() == slang::DeclReflection::Kind::Enum;
        }

        [[nodiscard]] bool IsExportedStruct(slang::DeclReflection* decl)
        {
            if (decl->getKind() != slang::DeclReflection::Kind::Struct) return false;

            slang::TypeReflection* type = decl->getType();
            return type && type->findUserAttributeByName(kExportAttribute);
        }

        [[nodiscard]] bool ExportsStructs(slang::IModule* module)
        {
            slang::DeclReflection* moduleDecl = module->getModuleReflection();
            for (uint32_t i = 0; i < moduleDecl->getChildrenCount(); ++i)
            {
                if (IsExportedStruct(moduleDecl->getChild(i))) return true;
            }

            return false;
        }

        // Enum cases, without the members Slang synthesizes ("$inheritance", "$__syn_...").
        [[nodiscard]] std::vector<std::string> GetEnumCaseNames(slang::DeclReflection* enumDecl)
        {
            std::vector<std::string> names;
            for (uint32_t i = 0; i < enumDecl->getChildrenCount(); ++i)
            {
                const char* name = enumDecl->getChild(i)->getName();
                if (name && name[0] != '\0' && name[0] != '$') names.emplace_back(name);
            }

            return names;
        }

        [[nodiscard]] const char* ScalarName(ScalarType type)
        {
            switch (type)
            {
                case ScalarType::Int8:    return "int8_t";
                case ScalarType::UInt8:   return "uint8_t";
                case ScalarType::Int16:   return "int16_t";
                case ScalarType::UInt16:  return "uint16_t";
                case ScalarType::Int32:   return "int32_t";
                case ScalarType::UInt32:  return "uint32_t";
                case ScalarType::Int64:   return "int64_t";
                case ScalarType::UInt64:  return "uint64_t";
                case ScalarType::Float32: return "float";
                case ScalarType::Float64: return "double";
                default:                  return nullptr;
            }
        }

        [[nodiscard]] const char* GlmVectorPrefix(ScalarType type)
        {
            switch (type)
            {
                case ScalarType::Int8:    return "i8vec";
                case ScalarType::UInt8:   return "u8vec";
                case ScalarType::Int16:   return "i16vec";
                case ScalarType::UInt16:  return "u16vec";
                case ScalarType::Int32:   return "ivec";
                case ScalarType::UInt32:  return "uvec";
                case ScalarType::Int64:   return "i64vec";
                case ScalarType::UInt64:  return "u64vec";
                case ScalarType::Float32: return "vec";
                case ScalarType::Float64: return "dvec";
                default:                  return nullptr;
            }
        }

        [[nodiscard]] const char* WhyNotScalar(ScalarType type)
        {
            switch (type)
            {
                case ScalarType::Bool:    return "bool is 4 bytes in a shader buffer but 1 byte in C++; use uint";
                case ScalarType::Float16: return "half has no C++ type; use float, or uint16_t and convert";
                default:                  return "this scalar type has no C++ equivalent";
            }
        }

        [[nodiscard]] std::string FormatFloat(float value, bool isDouble)
        {
            char buffer[64];
            const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
            std::string text(buffer, error == std::errc() ? end : buffer);
            if (text.find_first_of(".e") == std::string::npos) text += ".0";
            return isDouble ? text : text + "f";
        }

        [[nodiscard]] size_t AlignUp(size_t value, size_t alignment)
        {
            return alignment <= 1 ? value : (value + alignment - 1) / alignment * alignment;
        }

        struct StructMirror final
        {
            std::string Name;
            std::set<std::string> LocalDependencies;    // [CppExport] structs of the same module it contains
            std::string Text;                           // definition and static_asserts, unindented
        };

        class Generator final
        {
        public:
            Generator(slang::ISession* session, slang::IModule* module, const std::vector<CppExportRoot>& roots, std::string& diagnostics)
            : Session(session), Module(module), Roots(roots), Diagnostics(diagnostics) {}

            [[nodiscard]] bool Generate(const std::string& moduleName, std::string& outHeader)
            {
                if (!LocateModule(Module, Roots, Location))
                {
                    Diagnostics += "error: module '" + moduleName + "' is not under any directory C++ headers are generated for.\n";
                    return false;
                }

                std::vector<slang::DeclReflection*> exportedStructs;
                std::vector<slang::DeclReflection*> enums;
                slang::DeclReflection* moduleDecl = Module->getModuleReflection();
                for (uint32_t i = 0; i < moduleDecl->getChildrenCount(); ++i)
                {
                    slang::DeclReflection* decl = moduleDecl->getChild(i);
                    if (IsExportedStruct(decl)) exportedStructs.push_back(decl);
                    else if (decl->getKind() == slang::DeclReflection::Kind::Enum) enums.push_back(decl);
                }

                if (exportedStructs.empty()) return true;

                std::vector<std::string> enumTexts;
                if (!enums.empty() && !MirrorEnums(moduleName, enums, enumTexts)) return false;

                std::map<std::string, StructMirror> structs;
                std::vector<std::string> declarationOrder;
                for (slang::DeclReflection* decl : exportedStructs)
                {
                    StructMirror mirror = MirrorStruct(decl);
                    declarationOrder.push_back(mirror.Name);
                    structs.emplace(mirror.Name, std::move(mirror));
                }

                if (Failed) return false;

                outHeader = Assemble(enumTexts, OrderByDependencies(structs, declarationOrder));
                return true;
            }

        private:
            slang::ISession* Session;
            slang::IModule* Module;
            const std::vector<CppExportRoot>& Roots;
            std::string& Diagnostics;

            ModuleLocation Location;
            std::set<std::string> SystemIncludes;
            std::set<std::string> LocalIncludes;
            bool Failed = false;

            void Error(const std::string& message)
            {
                Diagnostics += "error: " + Location.SourceName + ": " + message + "\n";
                Failed = true;
            }

            struct Declaration final
            {
                slang::IModule* Module = nullptr;
                slang::DeclReflection* Decl = nullptr;
                ModuleLocation Location;
            };

            // The module a struct or enum used by a field is declared in. Only modules under one of the roots are
            // searched, which keeps Slang's own modules out.
            [[nodiscard]] Declaration FindDeclaration(slang::TypeReflection* type)
            {
                const char* name = type->getName();
                Declaration byName{};
                uint32_t numberOfNameMatches = 0;

                for (SlangInt i = 0; i < Session->getLoadedModuleCount(); ++i)
                {
                    slang::IModule* module = Session->getLoadedModule(i);
                    ModuleLocation location;
                    if (!LocateModule(module, Roots, location)) continue;

                    slang::DeclReflection* moduleDecl = module->getModuleReflection();
                    for (uint32_t c = 0; c < moduleDecl->getChildrenCount(); ++c)
                    {
                        slang::DeclReflection* decl = moduleDecl->getChild(c);
                        if (!IsTypeDeclaration(decl)) continue;

                        const Declaration declaration{.Module = module, .Decl = decl, .Location = location};
                        if (decl->getType() == type) return declaration;

                        if (name && decl->getName() && std::strcmp(name, decl->getName()) == 0)
                        {
                            byName = declaration;
                            ++numberOfNameMatches;
                        }
                    }
                }

                return numberOfNameMatches == 1 ? byName : Declaration{};
            }

            // A struct or enum from this module is used by name; one from another module through that module's header.
            [[nodiscard]] std::string MapDeclaredType(slang::TypeReflection* type, const std::string& where, StructMirror& inOutMirror)
            {
                const std::string name = type->getName() ? type->getName() : "<unnamed>";
                const Declaration declaration = FindDeclaration(type);
                if (!declaration.Module)
                {
                    Error(where + ": cannot find where " + name + " is declared; only types from the generated shader directories can be mirrored");
                    return {};
                }

                const bool isLocal = declaration.Module == Module;
                const bool isEnum = declaration.Decl->getKind() == slang::DeclReflection::Kind::Enum;
                if (!isEnum && !IsExportedStruct(declaration.Decl))
                {
                    Error(where + ": struct " + name + " is not marked [CppExport], so it has no C++ mirror");
                    return {};
                }

                if (isEnum && !isLocal && !ExportsStructs(declaration.Module))
                {
                    Error(where + ": enum " + name + " comes from a module without [CppExport] structs, so it has no C++ header. Export a struct from that module or move the enum");
                    return {};
                }

                if (isLocal)
                {
                    if (!isEnum) inOutMirror.LocalDependencies.insert(name);
                    return name;
                }

                LocalIncludes.insert("\"" + declaration.Location.Include + "\"");
                return Qualify(declaration.Location.Namespace, name);
            }

            [[nodiscard]] std::string MapType(slang::TypeReflection* type, slang::TypeLayoutReflection* typeLayout, const std::string& where, StructMirror& inOutMirror)
            {
                switch (type->getKind())
                {
                    case Kind::Scalar:
                    {
                        if (const char* name = ScalarName(type->getScalarType())) return name;
                        Error(where + ": " + WhyNotScalar(type->getScalarType()));
                        return {};
                    }

                    case Kind::Vector:
                    {
                        const ScalarType elementType = type->getElementType()->getScalarType();
                        const size_t count = type->getElementCount();
                        const char* prefix = GlmVectorPrefix(elementType);
                        if (!prefix || count < 1 || count > 4)
                        {
                            Error(where + ": " + (prefix ? std::string("vectors need 1 to 4 components") : WhyNotScalar(elementType)));
                            return {};
                        }

                        SystemIncludes.insert("<glm/glm.hpp>");
                        if (elementType != ScalarType::Float32 && elementType != ScalarType::Float64 && elementType != ScalarType::Int32 && elementType != ScalarType::UInt32)
                        {
                            SystemIncludes.insert("<glm/gtc/type_precision.hpp>");
                        }

                        return "glm::" + std::string(prefix) + std::to_string(count);
                    }

                    case Kind::Matrix:
                    {
                        const ScalarType elementType = type->getElementType()->getScalarType();
                        if (elementType != ScalarType::Float32 && elementType != ScalarType::Float64)
                        {
                            Error(where + ": only float and double matrices have a glm equivalent");
                            return {};
                        }

                        // Row-major RxC is R rows of C values, the same memory as glm's R columns of C values (glm::matRxC).
                        const uint32_t rows = type->getRowCount();
                        const uint32_t columns = type->getColumnCount();
                        SystemIncludes.insert("<glm/glm.hpp>");
                        const std::string prefix = elementType == ScalarType::Float32 ? "glm::mat" : "glm::dmat";
                        return rows == columns ? prefix + std::to_string(rows) : prefix + std::to_string(rows) + "x" + std::to_string(columns);
                    }

                    case Kind::Array:
                    {
                        const size_t count = type->getElementCount();
                        if (count == 0 || count == SLANG_UNBOUNDED_SIZE || count == SLANG_UNKNOWN_SIZE)
                        {
                            Error(where + ": arrays need a fixed size to be mirrored");
                            return {};
                        }

                        const std::string elementType = MapType(type->getElementType(), typeLayout->getElementTypeLayout(), where + "[]", inOutMirror);
                        if (elementType.empty()) return {};

                        SystemIncludes.insert("<array>");
                        return "std::array<" + elementType + ", " + std::to_string(count) + ">";
                    }

                    case Kind::Pointer:
                        return "uint64_t";

                    case Kind::Struct:
                    {
                        if (type->getName() && std::strcmp(type->getName(), "DescriptorHandle") == 0)
                        {
                            LocalIncludes.insert("\"descriptorHandle.h\"");
                            return "EOS::DescriptorHandle";
                        }

                        return MapDeclaredType(type, where, inOutMirror);
                    }

                    case Kind::Enum:
                        return MapDeclaredType(type, where, inOutMirror);

                    default:
                        Error(where + ": " + (type->getName() ? std::string(type->getName()) : std::string("this type")) +
                              " cannot be stored in memory shared with C++; reference resources with DescriptorHandle<T> and buffers with pointers");
                        return {};
                }
            }

            // Slang's reflection only exposes defaults that are literal numbers.
            [[nodiscard]] std::string MapDefault(slang::VariableReflection* field, slang::TypeReflection* type, const std::string& where)
            {
                if (!field->hasDefaultValue()) return "{}";

                if (type->getKind() == Kind::Scalar)
                {
                    const ScalarType scalarType = type->getScalarType();
                    const bool isFloat = scalarType == ScalarType::Float32 || scalarType == ScalarType::Float64;

                    float floatValue = 0.0f;
                    if (isFloat && SLANG_SUCCEEDED(field->getDefaultValueFloat(&floatValue)) && std::isfinite(floatValue))
                    {
                        return " = " + FormatFloat(floatValue, scalarType == ScalarType::Float64);
                    }

                    int64_t intValue = 0;
                    if (SLANG_SUCCEEDED(field->getDefaultValueInt(&intValue)))
                    {
                        return isFloat ? " = " + FormatFloat(static_cast<float>(intValue), scalarType == ScalarType::Float64) : " = " + std::to_string(intValue);
                    }
                }

                Error(where + ": its default value cannot be read through Slang's reflection, which only exposes literal numbers. Remove it (C++ starts the field zeroed) or make it a literal");
                return "{}";
            }

            [[nodiscard]] StructMirror MirrorStruct(slang::DeclReflection* decl)
            {
                slang::TypeReflection* type = decl->getType();
                StructMirror mirror{.Name = decl->getName()};

                slang::TypeLayoutReflection* layout = Session->getTypeLayout(type, 0, slang::LayoutRules::DefaultStructuredBuffer);
                if (!layout)
                {
                    Error(mirror.Name + ": Slang could not lay it out");
                    return mirror;
                }

                std::string fields;
                std::string asserts;
                size_t cppOffset = 0;

                for (uint32_t i = 0; i < type->getFieldCount(); ++i)
                {
                    slang::VariableReflection* field = type->getFieldByIndex(i);
                    slang::VariableLayoutReflection* fieldLayout = layout->getFieldByIndex(i);
                    slang::TypeLayoutReflection* fieldTypeLayout = fieldLayout->getTypeLayout();
                    const std::string fieldName = field->getName();
                    const std::string where = mirror.Name + "." + fieldName;

                    const std::string cppType = MapType(field->getType(), fieldTypeLayout, where, mirror);
                    if (cppType.empty()) continue;

                    // C++ places a field at the next multiple of its alignment after the previous field's sizeof, which
                    // for a struct includes its tail padding. Slang's scalar layout is the same rule; anything else
                    // cannot be expressed in C++.
                    const size_t slangOffset = fieldLayout->getOffset();
                    const size_t expectedOffset = AlignUp(cppOffset, static_cast<size_t>(fieldTypeLayout->getAlignment()));
                    if (slangOffset != expectedOffset)
                    {
                        Error(where + ": Slang places it at byte " + std::to_string(slangOffset) + " but C++ would at byte " + std::to_string(expectedOffset) + "; reorder the fields");
                    }

                    cppOffset = slangOffset + fieldTypeLayout->getStride();
                    fields += "    " + cppType + " " + fieldName + MapDefault(field, field->getType(), where) + ";\n";
                    asserts += "static_assert(offsetof(" + mirror.Name + ", " + fieldName + ") == " + std::to_string(slangOffset) +
                               ", \"Slang places " + where + " at byte " + std::to_string(slangOffset) + "\");\n";
                }

                const size_t stride = layout->getStride();
                mirror.Text = "struct " + mirror.Name + " final\n{\n" + fields + "};\n" +
                              "static_assert(sizeof(" + mirror.Name + ") == " + std::to_string(stride) + ", \"Slang's " + mirror.Name + " is " + std::to_string(stride) + " bytes\");\n" + asserts;
                return mirror;
            }

            // Slang does not expose enum case values directly, but it does expose the value of a constant, so the
            // values are read back through a module of constants that convert each case.
            [[nodiscard]] bool MirrorEnums(const std::string& moduleName, const std::vector<slang::DeclReflection*>& enums, std::vector<std::string>& outTexts)
            {
                std::string source = "import " + moduleName + ";\n";
                for (slang::DeclReflection* decl : enums)
                {
                    for (const std::string& caseName : GetEnumCaseNames(decl))
                    {
                        source += "public static const int64_t " + std::string(decl->getName()) + "_" + caseName + " = int64_t(" + decl->getName() + "." + caseName + ");\n";
                    }
                }

                std::string valuesModuleName = "eos_cpp_export_enum_values_" + moduleName;
                std::ranges::replace(valuesModuleName, '.', '_');

                Slang::ComPtr<ISlangBlob> diagnostics;
                slang::IModule* valuesModule = Session->loadModuleFromSourceString(valuesModuleName.c_str(), (valuesModuleName + ".slang").c_str(), source.c_str(), diagnostics.writeRef());
                if (!valuesModule)
                {
                    if (diagnostics) Diagnostics.append(static_cast<const char*>(diagnostics->getBufferPointer()), diagnostics->getBufferSize());
                    Error("could not read its enum values; enums shared with C++ have to be public");
                    return false;
                }

                std::map<std::string, int64_t> values;
                slang::DeclReflection* valuesDecl = valuesModule->getModuleReflection();
                for (uint32_t i = 0; i < valuesDecl->getChildrenCount(); ++i)
                {
                    slang::DeclReflection* decl = valuesDecl->getChild(i);
                    slang::VariableReflection* variable = decl->asVariable();
                    int64_t value = 0;
                    if (variable && decl->getName() && SLANG_SUCCEEDED(variable->getDefaultValueInt(&value))) values[decl->getName()] = value;
                }

                for (slang::DeclReflection* decl : enums)
                {
                    const std::string name = decl->getName();

                    // The layout of an enum is the layout of its underlying integer type.
                    const char* underlyingType = "int32_t";
                    if (slang::TypeLayoutReflection* layout = Session->getTypeLayout(decl->getType(), 0, slang::LayoutRules::DefaultStructuredBuffer))
                    {
                        if (layout->getType() && layout->getType()->getKind() == Kind::Scalar)
                        {
                            if (const char* scalarName = ScalarName(layout->getType()->getScalarType())) underlyingType = scalarName;
                        }
                    }

                    std::string text = "enum class " + name + " : " + underlyingType + "\n{\n";
                    for (const std::string& caseName : GetEnumCaseNames(decl))
                    {
                        const auto it = values.find(name + "_" + caseName);
                        if (it == values.end())
                        {
                            Error("could not read the value of " + name + "." + caseName);
                            continue;
                        }

                        text += "    " + caseName + " = " + std::to_string(it->second) + ",\n";
                    }

                    outTexts.push_back(text + "};\n");
                }

                return !Failed;
            }

            // Slang lets a struct use one declared further down; C++ does not.
            [[nodiscard]] static std::vector<const StructMirror*> OrderByDependencies(const std::map<std::string, StructMirror>& structs, const std::vector<std::string>& declarationOrder)
            {
                std::vector<const StructMirror*> ordered;
                std::set<std::string> emitted;

                std::function<void(const std::string&)> emit = [&](const std::string& name)
                {
                    const auto it = structs.find(name);
                    if (it == structs.end() || !emitted.insert(name).second) return;

                    for (const std::string& dependency : it->second.LocalDependencies) emit(dependency);
                    ordered.push_back(&it->second);
                };

                for (const std::string& name : declarationOrder) emit(name);
                return ordered;
            }

            [[nodiscard]] std::string Assemble(const std::vector<std::string>& enumTexts, const std::vector<const StructMirror*>& structs)
            {
                SystemIncludes.insert("<cstddef>");
                SystemIncludes.insert("<cstdint>");

                std::string header = "// Generated by EOSShaderCompilerTool from " + Location.SourceName + ". Do not edit: change the Slang source and build.\n"
                                     "// Scalar layout, as EOS uses for shader buffers, buffer pointers and push constants.\n"
                                     "#pragma once\n\n";

                // Standard headers, then glm, then EOS's own.
                std::string standardIncludes;
                std::string glmIncludes;
                std::string localIncludes;
                for (const std::string& include : SystemIncludes) (include.starts_with("<glm/") ? glmIncludes : standardIncludes) += "#include " + include + "\n";
                for (const std::string& include : LocalIncludes) localIncludes += "#include " + include + "\n";

                header += standardIncludes;
                if (!glmIncludes.empty()) header += "\n" + glmIncludes;
                if (!localIncludes.empty()) header += "\n" + localIncludes;

                const std::string indent = Location.Namespace.empty() ? "" : "    ";
                std::string body;
                const auto append = [&](const std::string& text)
                {
                    if (!body.empty()) body += "\n";
                    size_t lineStart = 0;
                    while (lineStart < text.size())
                    {
                        const size_t lineEnd = text.find('\n', lineStart);
                        body += indent + text.substr(lineStart, lineEnd - lineStart) + "\n";
                        lineStart = lineEnd + 1;
                    }
                };

                for (const std::string& text : enumTexts) append(text);
                for (const StructMirror* mirror : structs) append(mirror->Text);

                header += "\n";
                if (Location.Namespace.empty()) return header + body;
                return header + "namespace " + Location.Namespace + "\n{\n" + body + "}\n";
            }
        };
    }

    bool GenerateHeader(slang::ISession* session, slang::IModule* module, const std::string& moduleName, const std::vector<CppExportRoot>& roots, std::string& outHeader, std::string& outDiagnostics)
    {
        outHeader.clear();
        Generator generator(session, module, roots, outDiagnostics);
        return generator.Generate(moduleName, outHeader);
    }
}
#endif
