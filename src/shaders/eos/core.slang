// Constants shared by EOS shader modules, and the [CppExport] attribute.
module "eos/core";

// Marks a struct whose C++ mirror EOSShaderCompilerTool generates as ".generated/<module path>.h", so data shared
// with C++ (buffer contents, push constants) is declared once, here in Slang. The module's enums are mirrored with it.
// tools/ShaderTools/shaderCodegen.h lists which types can be mirrored and how.
[__AttributeUsage(_AttributeTargets.Struct)]
public struct CppExportAttribute {};

public static const float kPi        = 3.14159265358979323846;
public static const float kTwoPi     = 6.28318530717958647692;
public static const float kHalfPi    = 1.57079632679489661923;
public static const float kInvPi     = 0.31830988618379067154;
public static const float kInvTwoPi  = 0.15915494309189533577;
public static const float kFloatMax  = 3.402823466e+38;
