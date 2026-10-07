#include "../../Common/App.h"

// The whole frame is graphs/multiDrawIndirect.yaml: a glTF scene, a fly camera, and one pass written in Slang that
// draws every mesh of the scene with one indirect draw (shaders/forward.slang).
int main()
{
    ExampleApp App{{.Name = "EOS - MultiDrawIndirect"}};
    App.Run("multiDrawIndirect");
    return 0;
}
