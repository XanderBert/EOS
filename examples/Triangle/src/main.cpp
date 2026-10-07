#include "../../Common/App.h"

// The frame is graphs/triangle.yaml, and its one pass is shaders/triangle.slang: the registry loads the pass from its
// shader, so there is no pass code here.
int main()
{
    ExampleApp App{{.Name = "EOS - Render Triangle"}};
    App.Run("triangle");
    return 0;
}
