#pragma once
#include <EOS.h>




namespace EOS
{
    class VulkanContext : public IContext
    {
    public:
    DELETE_COPY_MOVE(VulkanContext)


    private:
        void GetDevices(HardwareDeviceType deviceType, HardwareDeviceDescription* outDevices, uint8_t maxOutDevices = 1);
    };

}
