#include "../../Common/App.h"

// The whole frame is graphs/edgeDetection.yaml: a glTF scene, a fly camera, and passes written in Slang (shaders/).
// Edit the file or the shaders while the example runs and press '-' to reload them.
int main()
{
    ExampleApp App{{.Name = "EOS - Deferred EdgeDetection"}};
    App.Run("edgeDetection");
    return 0;
}
